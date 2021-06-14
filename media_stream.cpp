#include "media_stream.h"

#include <arpa/inet.h>

#include "byte_buffer.h"
#include "server_config.h"
#include "spdlog/spdlog.h"
#include "utils.h"

RtpStoragePacket::RtpStoragePacket(uint32_t ssrc,
                                   uint16_t sequence_number,
                                   uint32_t timestamp,
                                   uint32_t header_offset,
                                   uint8_t* data,
                                   uint32_t size)
    : ssrc_{ssrc},
      sequence_number_{sequence_number},
      timestamp_{timestamp},
      header_offset_{header_offset},
      size_{size} {
  data_.reset(new uint8_t[size + kRtxExtraSize]);
  memcpy(data_.get(), data, size);
}

uint32_t RtpStoragePacket::GetSsrc() const {
  return ssrc_;
}

uint16_t RtpStoragePacket::GetSequenceNumber() const {
  return sequence_number_;
}

uint32_t RtpStoragePacket::GetTimestamp() const {
  return timestamp_;
}

uint8_t* RtpStoragePacket::Data() const {
  return data_.get();
}

uint32_t RtpStoragePacket::Size() const {
  if (is_rtx_) {
    return size_ + kRtxExtraSize;
  } else {
    return size_;
  }
}

void RtpStoragePacket::MakeRtxPacket(uint32_t ssrc,
                                     uint16_t sequence_number,
                                     uint8_t payload_type) {
  // https://tools.ietf.org/html/rfc4588#section-8.3
  // The format of a retransmission packet is shown below:

  //  0                   1                   2                   3
  //  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
  // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  // |                         RTP Header                            |
  // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  // |            OSN                |                               |
  // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               |
  // |                  Original RTP Packet Payload                  |
  // |                                                               |
  // +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
  FixedRtpHeader* rtp_header = (FixedRtpHeader*)(data_.get());
  if (!is_rtx_) {
    // Calculate payload length.
    int payload_len = size_ - header_offset_;
    // Move the payload data back two bytes.
    memmove(data_.get() + header_offset_ + 2, data_.get() + header_offset_,
            payload_len);
    // Fill OSN.
    memcpy(data_.get() + header_offset_, data_.get() + 2, 2);
    // Update ssrc, sequence number and payload type.
    rtp_header->SetSSrc(ssrc);
    rtp_header->SetSeqNum(sequence_number);
    rtp_header->SetPayloadType(payload_type);
  } else {
    rtp_header->SetSeqNum(sequence_number);
  }
  is_rtx_ = true;
}

uint64_t RtpStoragePacket::GetResendMillisecs() const {
  return resent_millisecs_;
}

void RtpStoragePacket::SetResendMillisecs(uint64_t millisecs) {
  resent_millisecs_ = millisecs;
}

RtpStream::RtpStream(const RtpParams& params, Observer* observer)
    : params_{params},
      observer_{observer},
      max_resend_delay_in_clock_rate_{params.clock_rate * 2} {}

std::unique_ptr<SenderReportPacket> RtpStream::CreateRtcpSenderReport(
    uint64_t now_millis) {
  if (send_packet_count_ == 0)
    return nullptr;
  auto ntp = NtpTime::CreateFromMillis(now_millis);
  auto sr = std::make_unique<SenderReportPacket>();
  auto diff_in_millis = now_millis - max_packet_millis_;
  auto diff_in_clockrate_ = diff_in_millis * params_.clock_rate / 1000;
  sr->SetSenderSsrc(params_.ssrc);
  sr->SetNtpSeconds(ntp.Seconds());
  sr->SetNtpFractions(ntp.Fractions());
  sr->SetRtpTimestamp(max_rtp_timestamp_ + diff_in_clockrate_);
  sr->SetSendPacketCount(send_packet_count_);
  sr->SendOctets(send_octets_);
  return sr;
}

void RtpStream::ReceiveReceiverReport(const ReportBlock& report_block) {
  uint64_t now = TimeMillis();
  NtpTime ntp = NtpTime::CreateFromMillis(now);
  uint32_t compact_ntp = ntp.ToCompactNtp();
  if (report_block.last_sr != 0) {
    uint32_t rtp_compact_ntp =
        compact_ntp - report_block.delay_since_last_sr - report_block.last_sr;
    rtt_ = NtpTime::CreateFromCompactNtp(rtp_compact_ntp).ToMillis();
  }
}

void RtpStream::ReceiveNack(NackPacket* nack_packet) {
  if (!nack_packet || nack_packet->GetMediaSsrc() != params_.ssrc ||
      !params_.is_nack_enable_)
    return;
  auto lost_packets = nack_packet->GetLostPacketSequenceNumbers();

  for (auto seq_num : lost_packets) {
    RtpStoragePacket* pkt = send_buffer_[seq_num % kSendBufferCapacity].get();
    if (!pkt)
      continue;

    if ((max_rtp_timestamp_ - pkt->GetTimestamp()) >
        max_resend_delay_in_clock_rate_) {
      continue;
    }

    uint64_t now = TimeMillis();
    if (pkt->GetResendMillisecs() != 0 &&
        now - pkt->GetResendMillisecs() <= static_cast<uint64_t>(rtt_)) {
      continue;
    }
    pkt->SetResendMillisecs(now);

    if (params_.is_rtx_enabled) {
      pkt->MakeRtxPacket(params_.rtx_ssrc, rtx_sequence_number_++,
                         params_.rtx_payload_type);
    }
    if (observer_)
      observer_->OnRtpStreamResendPacket(pkt);
  }
}

void RtpStream::ReceivePacket(RtpPacket* pkt) {
  if (!pkt || pkt->GetSsrc() != params_.ssrc)
    return;
  send_packet_count_++;
  send_octets_ += pkt->Size();
  max_rtp_timestamp_ = pkt->GetTimestamp();
  max_packet_millis_ = TimeMillis();

  if (params_.is_nack_enable_) {
    send_buffer_[pkt->GetSequenceNumber() % kSendBufferCapacity] =
        std::move(std::make_unique<RtpStoragePacket>(
            pkt->GetSsrc(), pkt->GetSequenceNumber(), pkt->GetTimestamp(),
            pkt->GetHeaderOffset(), pkt->Data(), pkt->Size()));
  }
}

MediaStream::MediaStream(boost::asio::io_context& io_context, Observer* observer)
    : io_context_{io_context}, observer_{observer} {
  rtcp_timer_ = std::make_unique<Timer>(io_context_, this);
  rtcp_timer_->AsyncWait(200);
}

void MediaStream::AddRtpStream(const RtpStream::RtpParams& params) {
  if (params.media_type == RtpStream::RtpParams::MediaType::kVideo) {
    h264_packetizer_ = std::make_unique<H264RtpPacketizer>(
        params.ssrc, params.payload_type, params.clock_rate, this);
  } else if (params.media_type == RtpStream::RtpParams::MediaType::kAudio) {
    opus_packetizer_ = std::make_unique<OpusRtpPacketizer>(
        params.ssrc, params.payload_type, params.clock_rate, this);
  }
  rtp_streams_[params.ssrc] = std::make_unique<RtpStream>(params, this);
}

void MediaStream::ReceiveH264Packet(MediaPacket::Pointer packet) {
  h264_packetizer_->Pack(packet);
}

void MediaStream::ReceiveOpusPacket(MediaPacket::Pointer packet) {
  opus_packetizer_->Pack(packet);
}

void MediaStream::ReceiveRctp(uint8_t* data, int len) {
  RtcpCompound rtcp_compound;
  if (!rtcp_compound.Parse(data, len)) {
    spdlog::warn("Failed to parse compound rtcp.");
    return;
  }
  auto rtcp_packets = rtcp_compound.GetRtcpPackets();
  for (auto p : rtcp_packets) {
    if (p->Type() == kRtcpTypeRtpfb) {
      if (p->Format() == 1) {
        NackPacket* nack_packet = dynamic_cast<NackPacket*>(p);
        rtp_streams_[nack_packet->GetMediaSsrc()]->ReceiveNack(nack_packet);
      } else if (p->Format() == 15) {
        // spdlog::debug("twcc .....");
      } else {
        spdlog::debug("fb format = {}", p->Format());
      }
    } else if (p->Type() == kRtcpTypeRr) {
      ReceiverReportPacket* rr_packet = dynamic_cast<ReceiverReportPacket*>(p);
      auto report_blocks = rr_packet->GetReportBlocks();
      for (auto block : report_blocks) {
        // When RTX is enabled, the RR packet of RTX is ignored.
        auto stream_iter = rtp_streams_.find(block.source_ssrc);
        if (stream_iter != rtp_streams_.end())
          stream_iter->second->ReceiveReceiverReport(block);
      }
    }
  }
}

void MediaStream::RtpPacketSent(RtpPacket* pkt) {
  rtp_streams_[pkt->GetSsrc()]->ReceivePacket(pkt);
}

void MediaStream::OnRtpStreamResendPacket(RtpStoragePacket* pkt) {
  observer_->OnRtpPacketSend(pkt->Data(), pkt->Size());
}

void MediaStream::OnRtpPacketGenerated(RtpPacket* pkt) {
  observer_->OnRtpPacketSend(pkt->Data(), pkt->Size());
  RtpPacketSent(pkt);
}

void MediaStream::OnTimerTimeout() {
  auto now_millis = TimeMillis();
  for (auto iter = rtp_streams_.begin(); iter != rtp_streams_.end(); ++iter) {
    auto sr_packet = iter->second->CreateRtcpSenderReport(now_millis);
    if (!sr_packet)
      continue;
    uint8_t buffer[1500];
    ByteWriter byte_write(buffer, 1500);
    if (sr_packet->Serialize(&byte_write)) {
      observer_->OnRtcpPacketSend(byte_write.Data(), byte_write.Used());
    }
  }

  rtcp_timer_->AsyncWait(200);
}

void MediaStream::Stop() {
  rtcp_timer_.reset();
}