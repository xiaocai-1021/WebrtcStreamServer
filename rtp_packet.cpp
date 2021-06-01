#include "rtp_packet.h"

#include <cassert>

#include "byte_buffer.h"

const uint8_t kNalTypeMask = 0x1F;
const uint8_t kStapA = 24;
const uint8_t kFuA = 28;
const uint8_t kFuStart = 0x80;
const uint8_t kFuEnd = 0x40;

void FixedRtpHeader::SetCC(uint8_t cc) {
  cc_ = cc;
}

void FixedRtpHeader::SetHasExtension(uint8_t has_extension) {
  has_extension_ = has_extension;
}

void FixedRtpHeader::SetPadding(uint8_t padding) {
  padding_ = padding;
}

void FixedRtpHeader::SetVersion(uint8_t version) {
  version_ = version;
}

void FixedRtpHeader::SetPayloadType(uint8_t payload_type) {
  payload_type_ = payload_type;
}

void FixedRtpHeader::SetMarker(uint8_t marker) {
  marker_ = marker;
}

void FixedRtpHeader::SetSeqNum(uint16_t seqnum) {
  StoreUInt16BE((uint8_t*)&seqnum_, seqnum);
}

void FixedRtpHeader::SetTimestamp(uint32_t timestamp) {
  StoreUInt32BE((uint8_t*)&timestamp_, timestamp);
}

void FixedRtpHeader::SetSSrc(uint32_t ssrc) {
  StoreUInt32BE((uint8_t*)&ssrc_, ssrc);
}

RtpPacketizer::RtpPacketizer(uint32_t ssrc,
                             uint8_t payload_type,
                             uint32_t clock_rate,
                             Observer* listener)
    : ssrc_{ssrc},
      payload_type_{payload_type},
      clock_rate_{clock_rate},
      listener_{listener} {}

H264RtpPacketizer::H264RtpPacketizer(uint32_t ssrc,
                                     uint8_t payload_type,
                                     uint32_t clock_rate,
                                     Observer* listener)
    : RtpPacketizer{ssrc, payload_type, clock_rate, listener} {}

void H264RtpPacketizer::Pack(MediaPacket::Pointer packet) {
  uint32_t nal_len = 0;
  uint32_t read_len = 0;
  bool is_key = false;
  using NaluStart = uint8_t*;
  using NaluLength = uint32_t;
  using NaluePosition = std::pair<NaluStart, NaluLength>;
  std::vector<NaluePosition> nalus;
  uint32_t timestamp = (double)packet->TimestampMillis() / 1000 * clock_rate_;

  while (read_len < packet->Size()) {
    nal_len = LoadUInt32BE(packet->Data() + read_len);
    read_len += 4;
    nalus.push_back(std::make_pair(packet->Data() + read_len, nal_len));
    read_len += nal_len;
  }

  assert(read_len == packet->Size());
  frame_end_marker_ = 0;
  for (int i = 0; i < nalus.size(); ++i) {
    if ((*nalus[i].first & 0x1F) == 5)
      PackStapA(packet->GetSideData(), timestamp);

    if (nalus.size() - i == 1)
      frame_end_marker_ = 1;
    if (nalus[i].second <= kMaxRtpPayloadSize) {
      PackSingNalu(nalus[i].first, nalus[i].second, timestamp);
    } else {
      PackFuA(nalus[i].first, nalus[i].second, timestamp);
    }
  }
}

void H264RtpPacketizer::PackStapA(std::vector<std::string> nalus,
                                  int64_t timestamp) {
  if (nalus.empty())
    return;
  uint8_t* p = rtp_buf_;
  FixedRtpHeader* rtp_hdr = (FixedRtpHeader*)p;
  rtp_hdr->SetPayloadType(payload_type_);
  rtp_hdr->SetSSrc(ssrc_);
  rtp_hdr->SetTimestamp(timestamp);
  rtp_hdr->SetSeqNum(seqnum_++);
  rtp_hdr->SetMarker(0);
  rtp_hdr->SetVersion(2);
  rtp_hdr->SetCC(0);
  rtp_hdr->SetHasExtension(0);
  rtp_hdr->SetPadding(0);
  p += kRtpHeaderFixedSize;

  uint8_t nalu_header = nalus[0][0];
  uint8_t stapA_header = kStapA;
  stapA_header |= (nalu_header & (~kNalTypeMask));
  *p++ = stapA_header;

  for (auto& nalu : nalus) {
    uint16_t size_u16 = nalu.size();
    uint8_t* pp = (uint8_t*)&size_u16;
    *p++ = pp[1];
    *p++ = pp[0];
    memcpy(p, nalu.data(), nalu.size());
    p += nalu.size();
  }

  if (listener_) {
    RtpPacket pkt(ssrc_, seqnum_ - 1, timestamp, kRtpHeaderFixedSize, rtp_buf_,
                  p - rtp_buf_);
    listener_->OnRtpPacketGenerated(&pkt);
  }
}

void H264RtpPacketizer::PackSingNalu(uint8_t* data,
                                     int size,
                                     uint32_t timestamp) {
  uint8_t* p = rtp_buf_;
  FixedRtpHeader* rtp_hdr = (FixedRtpHeader*)rtp_buf_;
  rtp_hdr->SetPayloadType(payload_type_);
  rtp_hdr->SetSSrc(ssrc_);
  rtp_hdr->SetTimestamp(timestamp);
  rtp_hdr->SetSeqNum(seqnum_++);
  rtp_hdr->SetMarker(frame_end_marker_);
  rtp_hdr->SetVersion(2);
  rtp_hdr->SetCC(0);
  rtp_hdr->SetHasExtension(0);
  rtp_hdr->SetPadding(0);
  p += kRtpHeaderFixedSize;
  memcpy(p, data, size);
  p += size;
  if (listener_) {
    RtpPacket pkt(ssrc_, seqnum_ - 1, timestamp, kRtpHeaderFixedSize, rtp_buf_,
                  p - rtp_buf_);
    listener_->OnRtpPacketGenerated(&pkt);
  }
}

void H264RtpPacketizer::PackFuA(uint8_t* data, int size, uint32_t timestamp) {
  bool start = true;
  bool end = false;
  uint8_t nalu_header = data[0];
  uint8_t fu_indicate = kFuA;
  fu_indicate |= (nalu_header & (~kNalTypeMask));

  int data_len = -1;
  data++;
  size--;
  while (!end) {
    if (size <= kMaxRtpPayloadSize - 2)
      end = true;
    uint8_t* p = rtp_buf_;
    FixedRtpHeader* rtp_hdr = (FixedRtpHeader*)p;
    rtp_hdr->SetPayloadType(payload_type_);
    rtp_hdr->SetSSrc(ssrc_);
    rtp_hdr->SetTimestamp(timestamp);
    rtp_hdr->SetSeqNum(seqnum_++);
    rtp_hdr->SetVersion(2);
    rtp_hdr->SetCC(0);
    rtp_hdr->SetHasExtension(0);
    rtp_hdr->SetPadding(0);
    p += kRtpHeaderFixedSize;
    *p++ = fu_indicate;

    uint8_t fu_header = nalu_header & kNalTypeMask;

    if (start) {
      fu_header |= kFuStart;
      start = false;
    }
    if (end) {
      fu_header |= kFuEnd;
      rtp_hdr->SetMarker(frame_end_marker_);
      data_len = size;
    } else {
      rtp_hdr->SetMarker(0);
      data_len = kMaxRtpPayloadSize - 2;
    }
    *p++ = fu_header;

    memcpy(p, data, data_len);
    p += data_len;
    if (listener_) {
      RtpPacket pkt(ssrc_, seqnum_ - 1, timestamp, kRtpHeaderFixedSize,
                    rtp_buf_, p - rtp_buf_);
      listener_->OnRtpPacketGenerated(&pkt);
    }

    data += data_len;
    size -= data_len;
  }
  assert(size == 0);
}

OpusRtpPacketizer::OpusRtpPacketizer(uint32_t ssrc,
                                     uint8_t payload_type,
                                     uint32_t clock_rate,
                                     Observer* listener)
    : RtpPacketizer{ssrc, payload_type, clock_rate, listener} {}

void OpusRtpPacketizer::Pack(MediaPacket::Pointer packet) {
  uint8_t* p = rtp_buf_;
  FixedRtpHeader* rtp_hdr = (FixedRtpHeader*)rtp_buf_;
  uint32_t timestamp = (double)packet->TimestampMillis() / 1000 * clock_rate_;

  rtp_hdr->SetPayloadType(payload_type_);
  rtp_hdr->SetSSrc(ssrc_);
  rtp_hdr->SetTimestamp(timestamp);
  rtp_hdr->SetSeqNum(seqnum_++);
  rtp_hdr->SetMarker(1);
  rtp_hdr->SetVersion(2);
  rtp_hdr->SetCC(0);
  rtp_hdr->SetHasExtension(0);
  rtp_hdr->SetPadding(0);
  p += kRtpHeaderFixedSize;
  memcpy(p, packet->Data(), packet->Size());
  p += packet->Size();
  if (listener_) {
    RtpPacket pkt(ssrc_, seqnum_ - 1, timestamp, kRtpHeaderFixedSize, rtp_buf_,
                  p - rtp_buf_);
    listener_->OnRtpPacketGenerated(&pkt);
  }
}