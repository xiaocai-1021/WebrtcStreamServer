#pragma once

#include <boost/asio.hpp>
#include <boost/thread/latch.hpp>
#include <memory>
#include <string>
#include <thread>
#include <cstddef>

#include "dtls_transport.h"
#include "ice_lite.h"
#include "media_packet.h"
#include "media_source.h"
#include "rtp_session.h"
#include "srtp_session.h"
#include "udp_socket.h"

class WebrtcTransport : public std::enable_shared_from_this<WebrtcTransport>,
                        public UdpSocket::Observer,
                        public IceLite::Observer,
                        public DtlsTransport::Observer,
                        public RtpSession::Observer,
                        public MediaSource::Observer {
 public:
  class Observer {
   public:
    virtual void OnWebrtcTransportShutdown(
        std::shared_ptr<WebrtcTransport> webrtc_transport) = 0;
  };
  WebrtcTransport(const std::string& stream_id);
  ~WebrtcTransport();

  std::string CreateAnswer();
  bool SetOffer(const std::string& offer);
  void RegisterObserver(Observer* observer);
  void DeregisterObserver();
  bool Start();
  void Stop();

 private:
  void WritePacket(char* buf, int len);
  void OnUdpSocketDataReceive(uint8_t* data,
                       size_t len,
                       udp::endpoint* remote_ep) override;
  void OnUdpSocketError() override;
  void OnStunMessageSend(uint8_t* data,
                         size_t size,
                         udp::endpoint* ep) override;
  void OnIceConnectionCompleted() override;
  void OnIceConnectionError() override;
  void OnDtlsTransportSetup(SrtpSession::CipherSuite suite,
                            uint8_t* localMasterKey,
                            int localMasterKeySize,
                            uint8_t* remoteMasterKey,
                            int remoteMasterKeySize) override;
  void OnDtlsTransportError() override;
  void OnDtlsTransportShutdown() override;
  void OnDtlsTransportSendData(const uint8_t* data, size_t len) override;
  void OnRtpPacketSend(uint8_t* data, int size) override;
  void OnRtcpPacketSend(uint8_t* data, int size) override;
  void OnIncomingH264Packet(MediaPacket::Pointer packet);
  void OnIncomingOpusPacket(MediaPacket::Pointer packet);
  void OnMediaPacketGenerated(MediaPacket::Pointer packet) override;
  void OnMediaSouceEnd() override;

  std::unique_ptr<SrtpSession> send_srtp_session_;
  std::unique_ptr<SrtpSession> recv_srtp_session_;
  std::unique_ptr<UdpSocket> udp_socket_;
  std::unique_ptr<IceLite> ice_lite_;
  std::unique_ptr<DtlsTransport> dtls_transport_;
  udp::endpoint selected_endpoint_;

  char protect_buffer_[65536];
  bool connection_established_;
  bool dtls_ready_{false};
  std::unique_ptr<RtpSession> rtp_session_;
  boost::asio::io_context message_loop_;
  std::thread work_thread_;
  Observer* observer_{nullptr};
  int32_t rtp_h264_payload_{-1};
  int32_t rtp_h264_rtx_payload_{-1};
  std::string ice_ufrag_;
  std::string ice_pwd_;
  std::string fingerprint_type_;
  std::string fingerprint_hash_;
  std::string remote_setup_;
  std::string stream_id_;
  boost::latch latch_{1};
};