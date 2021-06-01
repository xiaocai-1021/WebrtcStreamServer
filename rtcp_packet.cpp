#include "rtcp_packet.h"

#include <arpa/inet.h>

#include "spdlog/spdlog.h"

bool RtcpPacket::Parse(ByteReader* byte_reader) {
  if (!ParseCommonHeader(byte_reader))
    return false;

  int payload_len = header_.length * 4;
  if (!byte_reader->Consume(payload_len))
    return false;

  return true;
}

uint8_t RtcpPacket::Type() const {
  return header_.packet_type;
}

uint8_t RtcpPacket::Format() const {
  return header_.count_or_format;
}

uint8_t RtcpPacket::Count() const {
  return header_.count_or_format;
}

bool RtcpPacket::IsRtcp(uint8_t* data, size_t size) {
  // 72 to 76 is reserved for RTP
  // 77 to 79 is not reserver but  they are not assigned we will block them
  // for RTCP 200 SR  == marker bit + 72
  // for RTCP 204 APP == marker bit + 76
  /*
   *       RTCP
   *
   * FIR      full INTRA-frame request             192     [RFC2032]   supported
   * NACK     negative acknowledgement             193     [RFC2032]
   * IJ       Extended inter-arrival jitter report 195 [RFC-ietf-avt-rtp-toff
   * set-07.txt] http://tools.ietf.org/html/draft-ietf-avt-rtp-toffset-07
   * SR       sender report                        200     [RFC3551]   supported
   * RR       receiver report                      201     [RFC3551]   supported
   * SDES     source description                   202     [RFC3551]   supported
   * BYE      goodbye                              203     [RFC3551]   supported
   * APP      application-defined                  204     [RFC3551]   ignored
   * RTPFB    Transport layer FB message           205     [RFC4585]   supported
   * PSFB     Payload-specific FB message          206     [RFC4585]   supported
   * XR       extended report                      207     [RFC3611]   supported
   */

  /* 205       RFC 5104
   * FMT 1      NACK       supported
   * FMT 2      reserved
   * FMT 3      TMMBR      supported
   * FMT 4      TMMBN      supported
   */

  /* 206      RFC 5104
   * FMT 1:     Picture Loss Indication (PLI)                      supported
   * FMT 2:     Slice Lost Indication (SLI)
   * FMT 3:     Reference Picture Selection Indication (RPSI)
   * FMT 4:     Full Intra Request (FIR) Command                   supported
   * FMT 5:     Temporal-Spatial Trade-off Request (TSTR)
   * FMT 6:     Temporal-Spatial Trade-off Notification (TSTN)
   * FMT 7:     Video Back Channel Message (VBCM)
   * FMT 15:    Application layer FB message
   */

  if (size < kRtcpMinHeaderLength) {
    return false;
  }

  const uint8_t V = data[0] >> 6;
  if (V != kRtcpExpectedVersion) {
    return false;
  }

  const uint8_t payloadType = data[1];
  switch (payloadType) {
    case 192:
      return true;
    case 193:
      // not supported
      // pass through and check for a potential RTP packet
      return false;
    case 195:
    case 200:
    case 201:
    case 202:
    case 203:
    case 204:
    case 205:
    case 206:
    case 207:
      return true;
    default:
      return false;
  }
}

bool RtcpPacket::ParseCommonHeader(ByteReader* byte_reader) {
  if (!byte_reader->ReadBytes((char*)&header_, sizeof(RtcpCommonHeader)))
    return false;
  header_.length = ntohs(header_.length);
  return true;
}

bool RtcpPacket::SerializeCommonHeader(ByteWriter* byte_writer) {
  header_.length = htons(header_.length);
  if (!byte_writer->WriteBytes((const char*)(&header_),
                               sizeof(RtcpCommonHeader)))
    return false;
  return true;
}

bool SenderReportPacket::Serialize(ByteWriter* byte_writer) {
  header_.count_or_format = 0;
  header_.packet_type = kRtcpTypeSr;
  header_.padding = 0;
  header_.version = 2;
  header_.length = (sizeof(header_) + kSenderBaseLength) / 4 - 1;
  if (!SerializeCommonHeader(byte_writer))
    return false;
  if (!byte_writer->WriteUInt32(sender_ssrc_))
    return false;
  if (!byte_writer->WriteUInt32(ntp_seconds_))
    return false;
  if (!byte_writer->WriteUInt32(ntp_fractions_))
    return false;
  if (!byte_writer->WriteUInt32(rtp_timestamp_))
    return false;
  if (!byte_writer->WriteUInt32(send_packet_count_))
    return false;
  if (!byte_writer->WriteUInt32(send_octets_))
    return false;
  return true;
}

void SenderReportPacket::SetSenderSsrc(uint32_t sender_ssrc) {
  sender_ssrc_ = sender_ssrc;
}

void SenderReportPacket::SetNtpSeconds(uint32_t ntp_seconds) {
  ntp_seconds_ = ntp_seconds;
}

void SenderReportPacket::SetNtpFractions(uint32_t ntp_fractions) {
  ntp_fractions_ = ntp_fractions;
}

void SenderReportPacket::SetRtpTimestamp(uint32_t rtp_timestamp) {
  rtp_timestamp_ = rtp_timestamp;
}

void SenderReportPacket::SetSendPacketCount(uint32_t send_packet_count) {
  send_packet_count_ = send_packet_count;
}

void SenderReportPacket::SendOctets(uint32_t send_octets) {
  send_octets_ = send_octets;
}

bool ReceiverReportPacket::Parse(ByteReader* byte_reader) {
  if (!ParseCommonHeader(byte_reader))
    return false;
  if (!byte_reader->ReadUInt32(&sender_ssrc_))
    return false;
  for (int i = 0; i < Count(); ++i) {
    ReportBlock block;
    if (!byte_reader->ReadUInt32(&block.source_ssrc))
      return false;
    if (!byte_reader->ReadUInt8(&block.fraction_lost))
      return false;
    if (!byte_reader->ReadUInt24(&block.cumulative_lost))
      return false;
    if (!byte_reader->ReadUInt32(&block.extended_high_seq_num))
      return false;
    if (!byte_reader->ReadUInt32(&block.jitter))
      return false;
    if (!byte_reader->ReadUInt32(&block.last_sr))
      return false;
    if (!byte_reader->ReadUInt32(&block.delay_since_last_sr))
      return false;
    report_blocks_.push_back(block);
  }
  return true;
}

std::vector<ReportBlock> ReceiverReportPacket::GetReportBlocks() const {
  return report_blocks_;
}

bool RtpfbPacket::ParseCommonPeedback(ByteReader* byte_reader) {
  if (!byte_reader->ReadUInt32(&sender_ssrc_))
    return false;
  if (!byte_reader->ReadUInt32(&media_ssrc_))
    return false;
  return true;
}

uint32_t RtpfbPacket::GetMediaSsrc() {
  return media_ssrc_;
}

bool RtpfbPacket::Parse(ByteReader* byte_reader) {
  if (!ParseCommonHeader(byte_reader))
    return false;
  if (!ParseCommonPeedback(byte_reader))
    return false;
  int payload_len = header_.length * 4;
  if (!byte_reader->Consume(payload_len - kCommonFeedbackLength))
    return false;
  return true;
}

std::vector<uint16_t> NackPacket::GetLostPacketSequenceNumbers() {
  return packet_lost_sequence_numbers_;
}

bool NackPacket::Parse(ByteReader* byte_reader) {
  if (!ParseCommonHeader(byte_reader))
    return false;
  if (!ParseCommonPeedback(byte_reader))
    return false;
  int payload_len = header_.length * 4;
  size_t nack_items = (payload_len - kCommonFeedbackLength) / kNackItemLength;

  uint16_t pid = 0, blp = 0;
  for (size_t index = 0; index < nack_items; ++index) {
    if (!byte_reader->ReadUInt16(&pid))
      return false;
    if (!byte_reader->ReadUInt16(&blp))
      return false;
    packet_lost_sequence_numbers_.push_back(pid);

    pid = pid + 1;
    for (uint16_t bitmask = blp; bitmask != 0; bitmask >>= 1, ++pid) {
      if (bitmask & 1)
        packet_lost_sequence_numbers_.push_back(pid);
    }
  }

  return true;
}

bool RtcpCompound::Parse(uint8_t* data, int size) {
  ByteReader byte_reader(data, size);

  while (byte_reader.Left() >= 2) {
    RtcpPacket* packet = nullptr;
    RtcpCommonHeader* header = (RtcpCommonHeader*)byte_reader.CurrentData();
    if (header->packet_type == kRtcpTypeRtpfb) {
      if (1 == header->count_or_format) {
        packet = new NackPacket;
      } else {
        packet = new RtcpPacket;
      }

      // if(15 == header->count_or_format)
      // spdlog::info("Incomming TWCC packet.");
    } else if (header->packet_type == kRtcpTypeRr) {
      packet = new ReceiverReportPacket;
    } else {
      packet = new RtcpPacket;
    }

    if (!packet->Parse(&byte_reader)) {
      spdlog::warn("rtcp packet parse error.");
      return false;
    }
    rtcps_.push_back(packet);
  }

  return true;
}

std::vector<RtcpPacket*> RtcpCompound::GetRtcpPackets() {
  return rtcps_;
}

RtcpCompound::~RtcpCompound() {
  for (auto p : rtcps_) {
    if (p)
      delete p;
  }
}