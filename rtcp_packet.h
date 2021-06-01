#pragma once

#include <cstddef>
#include <set>
#include <vector>

#include "byte_buffer.h"

enum { kRtcpExpectedVersion = 2, kRtcpMinHeaderLength = 4 };

enum RtcpType {
  kRtcpTypeFir = 192,
  kRtcpTypeSr = 200,
  kRtcpTypeRr = 201,
  kRtcpTypeSdes = 202,
  kRtcpTypeBye = 203,
  kRtcpTypeApp = 204,
  kRtcpTypeRtpfb = 205,
  kRtcpTypePsfb = 206,
  kRtcpTypeXr = 207,
};

//    0                   1           1       2                   3
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 0 |V=2|P|   C/F   |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 1                 |  Packet Type  |
//   ----------------+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 2                                 |             length            |
//   --------------------------------+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
// Common header for all RTCP packets, 4 octets.
struct RtcpCommonHeader {
  uint8_t count_or_format : 5;
  uint8_t padding : 1;
  uint8_t version : 2;
  uint8_t packet_type : 8;
  uint16_t length : 16;
};

class RtcpPacket {
 public:
  virtual ~RtcpPacket() = default;
  virtual bool Parse(ByteReader* byte_reader);
  uint8_t Type() const;
  uint8_t Format() const;
  uint8_t Count() const;
  static bool IsRtcp(uint8_t* data, size_t size);

 protected:
  bool ParseCommonHeader(ByteReader* byte_reader);
  bool SerializeCommonHeader(ByteWriter* byte_writer);
  RtcpCommonHeader header_;
};

struct ReportBlock {
  static const size_t kLength = 24;
  uint32_t source_ssrc;
  uint8_t fraction_lost;
  uint32_t cumulative_lost;  // TODO FIXME :Signed 24-bit value
  uint32_t extended_high_seq_num;
  uint32_t jitter;
  uint32_t last_sr;
  uint32_t delay_since_last_sr;
};

class SenderReportPacket : public RtcpPacket {
 public:
  bool Serialize(ByteWriter* byte_writer);

  void SetSenderSsrc(uint32_t sender_ssrc);

  void SetNtpSeconds(uint32_t ntp_seconds);

  void SetNtpFractions(uint32_t ntp_fractions);

  void SetRtpTimestamp(uint32_t rtp_timestamp);

  void SetSendPacketCount(uint32_t send_packet_count);

  void SendOctets(uint32_t send_octets);

 private:
  static constexpr size_t kSenderBaseLength = 24;
  uint32_t sender_ssrc_{0};
  uint32_t ntp_seconds_;
  uint32_t ntp_fractions_;
  uint32_t rtp_timestamp_;
  uint32_t send_packet_count_;
  uint32_t send_octets_;
};

class ReceiverReportPacket : public RtcpPacket {
 public:
  bool Parse(ByteReader* byte_reader);

  std::vector<ReportBlock> GetReportBlocks() const;

 protected:
  std::vector<ReportBlock> report_blocks_;
  uint32_t sender_ssrc_{0};
};

// RFC 4585, Section 6.1: Feedback format.
//
// Common packet format:
//
//    0                   1                   2                   3
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |V=2|P|   FMT   |       PT      |          length               |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 0 |                  SSRC of packet sender                        |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 4 |                  SSRC of media source                         |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   :            Feedback Control Information (FCI)                 :
//   :                                                               :
class RtpfbPacket : public RtcpPacket {
 public:
  bool Parse(ByteReader* byte_reader) override;
  uint32_t GetMediaSsrc();

 protected:
  static constexpr size_t kCommonFeedbackLength = 8;
  bool ParseCommonPeedback(ByteReader* byte_reader);
  uint32_t sender_ssrc_{0};
  uint32_t media_ssrc_{0};
};

// RFC 4585: Feedback format.
//
// Common packet format:
//
//    0                   1                   2                   3
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |V=2|P|   FMT   |       PT      |          length               |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 0 |                  SSRC of packet sender                        |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
// 4 |                  SSRC of media source                         |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   :            Feedback Control Information (FCI)                 :
//   :                                                               :
//
// Generic NACK (RFC 4585).
//
// FCI:
//    0                   1                   2                   3
//    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//   |            PID                |             BLP               |
//   +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
class NackPacket : public RtpfbPacket {
 public:
  bool Parse(ByteReader* byte_reader) override;
  std::vector<uint16_t> GetLostPacketSequenceNumbers();

 private:
  static constexpr size_t kNackItemLength = 4;
  std::vector<uint16_t> packet_lost_sequence_numbers_;
};

class RtcpCompound {
 public:
  ~RtcpCompound();
  bool Parse(uint8_t* data, int size);
  std::vector<RtcpPacket*> GetRtcpPackets();

 private:
  std::vector<RtcpPacket*> rtcps_;
};