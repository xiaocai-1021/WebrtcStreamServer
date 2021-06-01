#include "webrtc_transport.h"

#include "sdptransform/json.hpp"
#include "sdptransform/sdptransform.hpp"
#include "dtls_context.h"
#include "media_source_manager.h"
#include "server_config.h"
#include "stun_message.h"

WebrtcTransport::WebrtcTransport(const std::string& stream_id)
    : connection_established_(false), stream_id_{stream_id} {}

void WebrtcTransport::OnMediaPacketGenerated(MediaPacket::Pointer packet) {
  latch_.wait();
  if (connection_established_) {
    if (packet->PacketType() == MediaPacket::Type::kVideo)
      message_loop_.post(
          std::bind(&WebrtcTransport::OnIncomingH264Packet, this, packet));
    else
      message_loop_.post(
          std::bind(&WebrtcTransport::OnIncomingOpusPacket, this, packet));
  } else {
    spdlog::error(
        "The connection was not established when the media package was "
        "written.");
  }
}

void WebrtcTransport::OnMediaSouceEnd() {
  latch_.wait();
  if (observer_)
    observer_->OnWebrtcTransportShutdown(shared_from_this());
}

bool WebrtcTransport::Start() {
  udp_socket_.reset(new UdpSocket(message_loop_, this, 5000));
  udp_socket_->ListenTo(ServerConfig::GetInstance().GetIp(), 0);
  ice_lite_.reset(new IceLite(ice_ufrag_, this));
  send_srtp_session_.reset(new SrtpSession());
  recv_srtp_session_.reset(new SrtpSession());
  dtls_transport_.reset(new DtlsTransport(message_loop_, this));
  dtls_transport_->SetRemoteFingerprint(fingerprint_type_,
                                        fingerprint_hash_.c_str());
  if (!dtls_transport_->Init())
    return false;
  if (work_thread_.get_id() == std::thread::id())
    work_thread_ =
        std::thread(boost::bind(&boost::asio::io_context::run, &message_loop_));
  return true;
}

void WebrtcTransport::RegisterObserver(Observer* observer) {
  observer_ = observer;
}

void WebrtcTransport::DeregisterObserver() {
  observer_ = nullptr;
}

WebrtcTransport::~WebrtcTransport() {
  auto media_source = MediaSourceManager::GetInstance().Query(stream_id_);
  media_source->DeregisterObserver(this);
  spdlog::debug("Call WebrtcTransport's destructor.");
}

void WebrtcTransport::Stop() {
  message_loop_.stop();
  udp_socket_->Close();
  if (dtls_transport_)
    dtls_transport_->Stop();
  if (rtp_session_)
    rtp_session_->Stop();

  if (work_thread_.joinable())
    work_thread_.join();
}

bool WebrtcTransport::SetOffer(const std::string& offer) {
  auto session = sdptransform::parse(offer);
  if (session.find("media") == session.end()) {
    spdlog::error("There is no media in offer SDP.");
    return false;
  }

  auto media = session.at("media");
  rtp_h264_payload_ = -1;
  rtp_h264_rtx_payload_ = -1;

  for (int i = 0; i < media.size(); ++i) {
    if (media[i].find("setup") == media[i].end()) {
      spdlog::error("There are no 'setup' in m-line of SDP.");
      return false;
    }

    remote_setup_ = media[i].at("setup");

    if (media[i].find("iceUfrag") == media[i].end() ||
        media[i].find("icePwd") == media[i].end()) {
      spdlog::error("There are no 'iceUfrag' and 'icePwd' in m-line of SDP.");
      return false;
    }

    if (ice_ufrag_.empty() && ice_pwd_.empty()) {
      ice_ufrag_ = media[i].at("iceUfrag");
      ice_pwd_ = media[i].at("icePwd");
    }

    if (media[i].find("fingerprint") == media[i].end()) {
      spdlog::error("There are no 'fingerprint' in m-line of SDP.");
      return false;
    }

    auto fingerprint = media[i].at("fingerprint");

    if (fingerprint_type_.empty() || fingerprint_hash_.empty()) {
      fingerprint_type_ = fingerprint.at("type");
      fingerprint_hash_ = fingerprint.at("hash");
    }

    if (fingerprint.find("type") == fingerprint.end() ||
        fingerprint.find("hash") == fingerprint.end())
      return false;

    if (media[i].at("type") == "video") {
      auto& video = media[i];
      if (video.find("rtp") == video.end())
        return false;
      auto& video_rtp = video.at("rtp");
      for (auto& rtp_item : video_rtp) {
        if (rtp_item.at("codec") == "H264") {
          rtp_h264_payload_ = rtp_item.at("payload");
        }
      }

      if (rtp_h264_payload_ == -1)
        return false;

      if (video.find("fmtp") != video.end()) {
        auto& fmtp = video.at("fmtp");
        std::string h264_rtx_config =
            "apt=" + std::to_string(rtp_h264_payload_);
        for (auto& fmtp_item : fmtp) {
          if (fmtp_item.at("config") == h264_rtx_config) {
            rtp_h264_rtx_payload_ = fmtp_item.at("payload");
          }
        }
      }
    }
  }
  return true;
}

std::string WebrtcTransport::CreateAnswer() {
  char sdp[1024 * 10] = {0};
  int nssrc = 12345678;
  uint16_t nport = 0;
  int h264_rtp_payload = 125;
  int h264_rtp_rtx_payload = 107;

  if (udp_socket_)
    nport = udp_socket_->GetListeningPort();
  std::string answer;
  try {
    nlohmann::json answe_jsonr;
    answe_jsonr["version"] = "0";
    nlohmann::json origin;
    origin["username"] = "-";
    origin["sessionId"] = 1495799811084970;
    origin["sessionVersion"] = 1495799811084970;
    origin["netType"] = "IN";
    origin["ipVer"] = 4;
    origin["adddress"] = "0.0.0.0";
    answe_jsonr["origin"] = origin;
    nlohmann::json timing;
    timing["start"] = 0;
    timing["stop"] = 0;
    answe_jsonr["timing"] = timing;
    answe_jsonr["iceUfrag"] = ice_lite_->GetLocalUfrag();
    answe_jsonr["icePwd"] = ice_lite_->GetLocalPassword();
    answe_jsonr["icelite"] = "ice-lite";
    answe_jsonr["setup"] = "active";
    nlohmann::json fingerprint;
    fingerprint["type"] = "sha-256";
    fingerprint["hash"] = DtlsContext::GetInstance().GetCertificateFingerPrint(
        DtlsContext::Hash::kSha256);
    answe_jsonr["fingerprint"] = fingerprint;
    nlohmann::json groups;
    groups[0]["type"] = "BUNDLE";
    groups[0]["mids"] = "0 1";
    answe_jsonr["groups"] = groups;
    nlohmann::json msidSemantic;
    msidSemantic["semantic"] = "WMS";
    msidSemantic["token"] = "WebrtcVODServer";
    answe_jsonr["msidSemantic"] = msidSemantic;

    answe_jsonr["media"] = nlohmann::json::array();

    nlohmann::json video_media;
    video_media["type"] = "video";
    video_media["port"] = 9;
    video_media["protocol"] = "UDP/TLS/RTP/SAVPF";
    video_media["payloads"] = std::to_string(h264_rtp_payload) + " " +
                              std::to_string(h264_rtp_rtx_payload);  // h264 rtx
    answe_jsonr["media"][0] = video_media;

    nlohmann::json ext;
    ext[0]["value"] = 1;
    ext[0]["uri"] =
        "http://www.ietf.org/id/"
        "draft-holmer-rmcat-transport-wide-cc-extensions-01";
    answe_jsonr["media"][0]["ext"] = ext;

    nlohmann::json video_connection;
    video_connection["version"] = 4;
    video_connection["ip"] = "0.0.0.0";
    answe_jsonr["media"][0]["connection"] = video_connection;

    answe_jsonr["media"][0]["mid"] = "0";
    answe_jsonr["media"][0]["direction"] = "sendonly";
    answe_jsonr["media"][0]["rtcpMux"] = "rtcp-mux";
    answe_jsonr["media"][0]["msid"] = "WebrtcVODServer VideoTrackId";

    nlohmann::json rtcpFb;
    rtcpFb[0]["payload"] = h264_rtp_payload;
    rtcpFb[0]["type"] = "nack";
    rtcpFb[1]["payload"] = h264_rtp_payload;
    rtcpFb[1]["type"] = "transport-cc";
    answe_jsonr["media"][0]["rtcpFb"] = rtcpFb;

    answe_jsonr["media"][0]["rtp"] = nlohmann::json::array();
    nlohmann::json video_h264_rtp;
    video_h264_rtp["payload"] = h264_rtp_payload;
    video_h264_rtp["codec"] = "H264";
    video_h264_rtp["rate"] = 90000;
    answe_jsonr["media"][0]["rtp"][0] = video_h264_rtp;

    nlohmann::json video_h264_rtx;
    video_h264_rtx["payload"] = h264_rtp_rtx_payload;
    video_h264_rtx["codec"] = "rtx";
    video_h264_rtx["rate"] = 90000;
    answe_jsonr["media"][0]["rtp"][1] = video_h264_rtx;
    nlohmann::json fmtp;
    fmtp[0]["payload"] = h264_rtp_rtx_payload;
    fmtp[0]["config"] = "apt=" + std::to_string(h264_rtp_payload);
    answe_jsonr["media"][0]["fmtp"] = fmtp;

    nlohmann::json video_candidate;
    video_candidate["foundation"] = "4";
    video_candidate["component"] = 1;
    video_candidate["transport"] = "udp";
    video_candidate["priority"] = 2130706431;
    video_candidate["ip"] = ServerConfig::GetInstance().GetAnnouncedIp();
    video_candidate["port"] = nport;
    video_candidate["type"] = "host";
    answe_jsonr["media"][0]["candidates"] = nlohmann::json::array();
    answe_jsonr["media"][0]["candidates"][0] = video_candidate;

    nlohmann::json ssrc_groups;
    ssrc_groups[0]["semantics"] = "FID";
    ssrc_groups[0]["ssrcs"] = "12345678 9527";
    answe_jsonr["media"][0]["ssrcGroups"] = ssrc_groups;

    answe_jsonr["media"][0]["ssrcs"] = nlohmann::json::array();
    nlohmann::json cname_ssrc;
    cname_ssrc["id"] = nssrc;
    cname_ssrc["attribute"] = "cname";
    cname_ssrc["value"] = "wvod";
    answe_jsonr["media"][0]["ssrcs"].push_back(cname_ssrc);

    nlohmann::json msid_ssrc;
    msid_ssrc["id"] = nssrc;
    msid_ssrc["attribute"] = "msid";
    msid_ssrc["value"] = "WebrtcVODServer VideoTrackId";
    answe_jsonr["media"][0]["ssrcs"].push_back(msid_ssrc);

    nlohmann::json mslabel_ssrc;
    mslabel_ssrc["id"] = nssrc;
    mslabel_ssrc["attribute"] = "mslabel";
    mslabel_ssrc["value"] = "WebrtcVODServer";
    answe_jsonr["media"][0]["ssrcs"].push_back(mslabel_ssrc);

    nlohmann::json label_ssrc;
    label_ssrc["id"] = nssrc;
    label_ssrc["attribute"] = "label";
    label_ssrc["value"] = "VideoTrackId";
    answe_jsonr["media"][0]["ssrcs"].push_back(label_ssrc);

    nlohmann::json rtx_cname_ssrc;
    rtx_cname_ssrc["id"] = 9527;
    rtx_cname_ssrc["attribute"] = "cname";
    rtx_cname_ssrc["value"] = "wvod";
    answe_jsonr["media"][0]["ssrcs"].push_back(rtx_cname_ssrc);

    nlohmann::json rtx_msid_ssrc;
    rtx_msid_ssrc["id"] = 9527;
    rtx_msid_ssrc["attribute"] = "msid";
    rtx_msid_ssrc["value"] = "WebrtcVODServer VideoTrackId";
    answe_jsonr["media"][0]["ssrcs"].push_back(rtx_msid_ssrc);

    nlohmann::json rtx_mslabel_ssrc;
    rtx_mslabel_ssrc["id"] = 9527;
    rtx_mslabel_ssrc["attribute"] = "mslabel";
    rtx_mslabel_ssrc["value"] = "WebrtcVODServer";
    answe_jsonr["media"][0]["ssrcs"].push_back(rtx_mslabel_ssrc);

    nlohmann::json rtx_label_ssrc;
    rtx_label_ssrc["id"] = 9527;
    rtx_label_ssrc["attribute"] = "label";
    rtx_label_ssrc["value"] = "VideoTrackId";
    answe_jsonr["media"][0]["ssrcs"].push_back(rtx_label_ssrc);

    {
      nlohmann::json audio_media;
      audio_media["type"] = "audio";
      audio_media["port"] = 9;
      audio_media["protocol"] = "UDP/TLS/RTP/SAVPF";
      audio_media["payloads"] = "111";
      answe_jsonr["media"][1] = audio_media;

      nlohmann::json ext;
      ext[0]["value"] = 1;
      ext[0]["uri"] =
          "http://www.ietf.org/id/"
          "draft-holmer-rmcat-transport-wide-cc-extensions-01";
      answe_jsonr["media"][1]["ext"] = ext;

      nlohmann::json audio_connection;
      audio_connection["version"] = 4;
      audio_connection["ip"] = "0.0.0.0";
      answe_jsonr["media"][1]["connection"] = audio_connection;

      answe_jsonr["media"][1]["mid"] = "1";
      answe_jsonr["media"][1]["direction"] = "sendonly";
      answe_jsonr["media"][1]["rtcpMux"] = "rtcp-mux";
      answe_jsonr["media"][1]["msid"] = "WebrtcVODServer AudioTrackId";

      nlohmann::json rtcpFb;
      rtcpFb[0]["payload"] = 111;
      rtcpFb[0]["type"] = "transport-cc";
      answe_jsonr["media"][1]["rtcpFb"] = rtcpFb;

      answe_jsonr["media"][1]["rtp"] = nlohmann::json::array();
      nlohmann::json audio_opus_rtp;
      audio_opus_rtp["payload"] = 111;
      audio_opus_rtp["codec"] = "opus";
      audio_opus_rtp["rate"] = 48000;
      audio_opus_rtp["encoding"] = "2";
      answe_jsonr["media"][1]["rtp"][0] = audio_opus_rtp;

      nlohmann::json fmtp;
      fmtp[0]["payload"] = 111;
      fmtp[0]["config"] = "minptime=20;useinbandfec=1";
      answe_jsonr["media"][1]["fmtp"] = fmtp;

      nlohmann::json video_candidate;
      video_candidate["foundation"] = "4";
      video_candidate["component"] = 1;
      video_candidate["transport"] = "udp";
      video_candidate["priority"] = 2130706431;
      video_candidate["ip"] = ServerConfig::GetInstance().GetAnnouncedIp();
      video_candidate["port"] = nport;
      video_candidate["type"] = "host";
      answe_jsonr["media"][1]["candidates"] = nlohmann::json::array();
      answe_jsonr["media"][1]["candidates"][0] = video_candidate;

      answe_jsonr["media"][1]["ssrcs"] = nlohmann::json::array();
      nlohmann::json cname_ssrc;
      cname_ssrc["id"] = 87654321;
      cname_ssrc["attribute"] = "cname";
      cname_ssrc["value"] = "wvod";
      answe_jsonr["media"][1]["ssrcs"].push_back(cname_ssrc);

      nlohmann::json msid_ssrc;
      msid_ssrc["id"] = 87654321;
      msid_ssrc["attribute"] = "msid";
      msid_ssrc["value"] = "WebrtcVODServer AudioTrackId";
      answe_jsonr["media"][1]["ssrcs"].push_back(msid_ssrc);

      nlohmann::json mslabel_ssrc;
      mslabel_ssrc["id"] = 87654321;
      mslabel_ssrc["attribute"] = "mslabel";
      mslabel_ssrc["value"] = "WebrtcVODServer";
      answe_jsonr["media"][1]["ssrcs"].push_back(mslabel_ssrc);

      nlohmann::json label_ssrc;
      label_ssrc["id"] = 87654321;
      label_ssrc["attribute"] = "label";
      label_ssrc["value"] = "AudioTrackId";
      answe_jsonr["media"][1]["ssrcs"].push_back(label_ssrc);
    }

    answer = sdptransform::write(answe_jsonr);
  } catch (std::exception& e) {
    spdlog::error("{}", e.what());
  }

  rtp_session_ = std::make_unique<RtpSession>(message_loop_, this);
  RtpStream::RtpParams video_rtp_params;
  video_rtp_params.ssrc = 12345678;
  video_rtp_params.clock_rate = 90000;
  video_rtp_params.payload_type = h264_rtp_payload;
  video_rtp_params.rtx_ssrc = 9527;
  video_rtp_params.rtx_payload_type = h264_rtp_rtx_payload;
  video_rtp_params.is_rtx_enabled = true;
  video_rtp_params.is_nack_enable_ = true;
  video_rtp_params.media_type = RtpStream::RtpParams::MediaType::kVideo;
  rtp_session_->AddRtpStream(video_rtp_params);
  RtpStream::RtpParams audio_rtp_params;
  audio_rtp_params.ssrc = 87654321;
  audio_rtp_params.clock_rate = 48000;
  audio_rtp_params.payload_type = 111;
  audio_rtp_params.is_nack_enable_ = true;
  audio_rtp_params.media_type = RtpStream::RtpParams::MediaType::kAudio;
  rtp_session_->AddRtpStream(audio_rtp_params);
  return answer;
}

void WebrtcTransport::WritePacket(char* buf, int len) {
  if (udp_socket_)
    udp_socket_->SendData(reinterpret_cast<uint8_t*>(buf), len,
                          &selected_endpoint_);
}

void WebrtcTransport::OnRtcpPacketSend(uint8_t* data, int size) {
  memcpy(protect_buffer_, data, size);
  int length = 0;
  send_srtp_session_->ProtectRtcp(protect_buffer_, size, 65536, &length);

  if (udp_socket_)
    udp_socket_->SendData(reinterpret_cast<uint8_t*>(protect_buffer_), length,
                          &selected_endpoint_);
}

void WebrtcTransport::OnRtpPacketSend(uint8_t* data, int size) {
  memcpy(protect_buffer_, data, size);
  int length = 0;
  send_srtp_session_->ProtectRtp(protect_buffer_, size, 65536, &length);

  if (udp_socket_)
    udp_socket_->SendData(reinterpret_cast<uint8_t*>(protect_buffer_), length,
                          &selected_endpoint_);
}

void WebrtcTransport::OnIncomingH264Packet(MediaPacket::Pointer packet) {
  rtp_session_->ReceiveH264Packet(packet);
}

void WebrtcTransport::OnIncomingOpusPacket(MediaPacket::Pointer packet) {
  rtp_session_->ReceiveOpusPacket(packet);
}

void WebrtcTransport::OnUdpSocketDataReceive(uint8_t* data,
                                      size_t len,
                                      udp::endpoint* remote_ep) {
  if (StunMessage::IsStun(data, len)) {
    ice_lite_->ProcessStunMessage(data, len, remote_ep);
  } else if (DtlsContext::IsDtls(data, len)) {
    if (dtls_ready_)
      dtls_transport_->ProcessDataFromPeer(data, len);
    else
      spdlog::warn("Dtls is not ready yet.");
  } else if (RtcpPacket::IsRtcp(data, len)) {
    int length = 0;
    if (!recv_srtp_session_->UnprotectRtcp(data, len, &length)) {
      spdlog::warn("Failed to unprotect the incoming RTCP packet.");
    }
    rtp_session_->ReceiveRctp(data, length);
  } else {
    // TODO.
  }
}

void WebrtcTransport::OnUdpSocketError() {
  spdlog::error("Udp socket error.");
  latch_.try_count_down();
  if (observer_)
    observer_->OnWebrtcTransportShutdown(shared_from_this());
}

void WebrtcTransport::OnStunMessageSend(uint8_t* data,
                                        size_t size,
                                        udp::endpoint* ep) {
  if (udp_socket_)
    udp_socket_->SendData(data, size, ep);
}

void WebrtcTransport::OnIceConnectionCompleted() {
  selected_endpoint_ = *ice_lite_->GetFavoredCandidate();

  if (!dtls_transport_->Start(remote_setup_)) {
    spdlog::error("DtlsTransport start failed!");
  } else {
    dtls_ready_ = true;
  }
}

void WebrtcTransport::OnIceConnectionError() {
  spdlog::error("Ice connection error occurred.");
  latch_.try_count_down();
  if (observer_)
    observer_->OnWebrtcTransportShutdown(shared_from_this());
}

void WebrtcTransport::OnDtlsTransportSendData(const uint8_t* data, size_t len) {
  this->WritePacket((char*)data, len);
}

void WebrtcTransport::OnDtlsTransportSetup(SrtpSession::CipherSuite suite,
                                           uint8_t* localMasterKey,
                                           int localMasterKeySize,
                                           uint8_t* remoteMasterKey,
                                           int remoteMasterKeySize) {
  spdlog::debug("DTLS ready.");
  if (!send_srtp_session_->Init(false, suite, localMasterKey,
                                localMasterKeySize))
    spdlog::error("Srtp send session init failed.");
  if (!recv_srtp_session_->Init(true, suite, remoteMasterKey,
                                remoteMasterKeySize))
    spdlog::error("Srtp revc session init failed.");
  connection_established_ = true;
  latch_.try_count_down();
}

void WebrtcTransport::OnDtlsTransportError() {
  spdlog::error("Dtls setup error.");
  latch_.try_count_down();
  if (observer_)
    observer_->OnWebrtcTransportShutdown(shared_from_this());
}

void WebrtcTransport::OnDtlsTransportShutdown() {
  spdlog::debug("Dtls is shutdown.");
  latch_.try_count_down();
  if (observer_)
    observer_->OnWebrtcTransportShutdown(shared_from_this());
}
