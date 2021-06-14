#pragma once

#include <array>
#include <boost/asio.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "media_packet.h"
#include "rtcp_packet.h"
#include "rtp_packet.h"
#include "timer.h"

class RtpStoragePacket {
 public:
  RtpStoragePacket(uint32_t ssrc,
                   uint16_t sequence_number,
                   uint32_t timestamp,
                   uint32_t header_offset,
                   uint8_t* data,
                   uint32_t size);
  uint32_t GetSsrc() const;

  uint16_t GetSequenceNumber() const;

  uint32_t GetTimestamp() const;

  uint8_t* Data() const;

  uint32_t Size() const;

  void MakeRtxPacket(uint32_t ssrc,
                     uint16_t sequence_number,
                     uint8_t payload_type);

  uint64_t GetResendMillisecs() const;

  void SetResendMillisecs(uint64_t millisecs);

 private:
  constexpr static uint32_t kRtxExtraSize = 2;
  uint32_t ssrc_;
  uint16_t sequence_number_;
  uint32_t size_;
  std::unique_ptr<uint8_t[]> data_;
  uint64_t resent_millisecs_{0};
  uint32_t timestamp_{0};
  bool is_rtx_{false};
  uint32_t header_offset_;
};

class RtpStream {
 public:
  static constexpr uint64_t kDefaultRttMillis = 100;
  static constexpr uint32_t kSendBufferCapacity = 1000;

  class RtpParams {
   public:
    enum class MediaType { kVideo, kAudio };
    MediaType media_type;
    uint32_t ssrc;
    uint32_t clock_rate;
    uint8_t payload_type;
    uint32_t rtx_ssrc{0};
    uint8_t rtx_payload_type{0};
    bool is_rtx_enabled{false};
    bool is_nack_enable_{false};
    bool is_twcc_enable_{false};
    uint8_t twcc_extension_id_{0};
  };

  class Observer {
   public:
    virtual void OnRtpStreamResendPacket(RtpStoragePacket* pkt) = 0;
  };

  RtpStream(const RtpParams& params, Observer* observer);

  std::unique_ptr<SenderReportPacket> CreateRtcpSenderReport(
      uint64_t now_millis);

  void ReceiveReceiverReport(const ReportBlock& report_block);

  void ReceiveNack(NackPacket* nack_packet);

  void ReceivePacket(RtpPacket* pkt);

 private:
  uint32_t max_rtp_timestamp_{0};
  uint32_t max_packet_millis_{0};
  uint64_t rtt_{kDefaultRttMillis};
  std::array<std::unique_ptr<RtpStoragePacket>, kSendBufferCapacity>
      send_buffer_;
  RtpParams params_;
  Observer* observer_;
  uint32_t max_resend_delay_in_clock_rate_;
  uint32_t send_packet_count_{0};
  uint32_t send_octets_{0};
  uint16_t rtx_sequence_number_{0};
};

class MediaStream : public RtpStream::Observer,
                   public Timer::Listener,
                   public RtpPacketizer::Observer {
 public:
  class Observer {
   public:
    virtual void OnRtcpPacketSend(uint8_t* data, int size) = 0;
    virtual void OnRtpPacketSend(uint8_t* data, int size) = 0;
  };

  MediaStream(boost::asio::io_context& io_context, Observer* observer);

  void AddRtpStream(const RtpStream::RtpParams& params);

  void ReceiveH264Packet(MediaPacket::Pointer packet);

  void ReceiveOpusPacket(MediaPacket::Pointer packet);

  void ReceiveRctp(uint8_t* data, int len);

  void Stop();

 private:
  void RtpPacketSent(RtpPacket* pkt);

  void OnRtpStreamResendPacket(RtpStoragePacket* pkt) override;

  void OnRtpPacketGenerated(RtpPacket* pkt) override;

  void OnTimerTimeout() override;

  boost::asio::io_context& io_context_;
  std::unordered_map<uint32_t, std::unique_ptr<RtpStream>> rtp_streams_;
  std::unique_ptr<Timer> rtcp_timer_;
  std::unique_ptr<H264RtpPacketizer> h264_packetizer_;
  std::unique_ptr<OpusRtpPacketizer> opus_packetizer_;
  Observer* observer_;
};