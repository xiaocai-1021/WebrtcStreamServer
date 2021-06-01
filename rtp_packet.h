#pragma once

#include <cstdint>
#include <type_traits>
#include <vector>

#include "media_packet.h"

constexpr uint32_t kMaxRtpPayloadSize = 1200;
constexpr uint32_t kRtpHeaderFixedSize = 12;

class FixedRtpHeader {
 public:
  void SetCC(uint8_t cc);

  void SetHasExtension(uint8_t has_extension);

  void SetPadding(uint8_t padding);

  void SetVersion(uint8_t version);

  void SetPayloadType(uint8_t payload_type);

  void SetMarker(uint8_t marker);

  void SetSeqNum(uint16_t seqnum);

  void SetTimestamp(uint32_t timestamp);

  void SetSSrc(uint32_t ssrc);

 private:
  uint8_t cc_ : 4;
  uint8_t has_extension_ : 1;
  uint8_t padding_ : 1;
  uint8_t version_ : 2;
  uint8_t payload_type_ : 7;
  uint8_t marker_ : 1;
  uint16_t seqnum_;
  uint32_t timestamp_;
  uint32_t ssrc_;
};

static_assert(std::is_trivially_copyable<FixedRtpHeader>::value, "");

class RtpPacket {
 public:
  RtpPacket(uint32_t ssrc,
            uint16_t sequence_number,
            uint32_t timestamp,
            uint32_t header_offset,
            uint8_t* data,
            uint32_t size)
      : ssrc_{ssrc},
        sequence_number_{sequence_number},
        timestamp_{timestamp},
        header_offset_{header_offset},
        data_{data},
        size_{size} {}

  uint32_t GetSsrc() const {
    return ssrc_;
  }

  uint16_t GetSequenceNumber() const {
    return sequence_number_;
  }

  uint32_t GetTimestamp() const {
    return timestamp_;
  }

  uint32_t GetHeaderOffset() const {
    return header_offset_;
  }

  uint32_t Size() const {
    return size_;
  }

  uint8_t* Data() const {
    return data_;
  }

 private:
  uint32_t ssrc_;
  uint16_t sequence_number_;
  uint32_t timestamp_;
  uint32_t header_offset_;
  uint32_t size_;
  uint8_t* data_;
};

class RtpPacketizer {
 public:
  class Observer {
   public:
    virtual void OnRtpPacketGenerated(RtpPacket* pkt) = 0;
  };

  RtpPacketizer(uint32_t ssrc,
                uint8_t payload_type,
                uint32_t clock_rate,
                Observer* listener);
  virtual void Pack(MediaPacket::Pointer packet) = 0;

 protected:
  static constexpr uint32_t kRtpBufferSize = 5000;
  uint16_t seqnum_{0};
  uint8_t payload_type_{0};
  uint32_t clock_rate_;
  uint32_t ssrc_{0};
  Observer* listener_{nullptr};
  uint8_t rtp_buf_[kRtpBufferSize];
  bool have_twcc_extension_{false};
};

class H264RtpPacketizer : public RtpPacketizer {
 public:
  H264RtpPacketizer(uint32_t ssrc,
                    uint8_t payload_type,
                    uint32_t clock_rate,
                    Observer* listener);
  void Pack(MediaPacket::Pointer packet) override;

 private:
  void PackSingNalu(uint8_t* data, int size, uint32_t timestamp);
  void PackFuA(uint8_t* data, int size, uint32_t timestamp);
  void PackStapA(std::vector<std::string> nalus, int64_t timestamp);
  uint8_t frame_end_marker_{0};
};

class OpusRtpPacketizer : public RtpPacketizer {
 public:
  OpusRtpPacketizer(uint32_t ssrc,
                    uint8_t payload_type,
                    uint32_t clock_rate,
                    Observer* listener);
  void Pack(MediaPacket::Pointer packet) override;
};