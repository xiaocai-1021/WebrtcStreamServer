#include "server_config.h"

#include "toml.hpp"
#include "spdlog/spdlog.h"

ServerConfig& ServerConfig::GetInstance() {
  static ServerConfig server_config;
  return server_config;
}

bool ServerConfig::Load(boost::string_view json_file_name) {
  try {
    const auto data = toml::parse(json_file_name.data());
    ip_ = toml::find<std::string>(data, "ip");
    announced_ip_ = toml::find<std::string>(data, "announcedIp");
    signaling_server_port_  = toml::find<uint16_t>(data, "signalingServerPort");
    webrtc_min_port_ = toml::find<uint16_t>(data, "webrtcMinPort");
    webrtc_max_port_ = toml::find<uint16_t>(data, "webrtcMaxPort");
    enable_gop_cache_ = toml::find<bool>(data, "enableGopCache");
  } catch (...) {
    spdlog::error("Parse config file failed.");
    return false;
  }

  return true;
}

const std::string& ServerConfig::GetIp() const {
  return ip_;
}

const std::string& ServerConfig::GetAnnouncedIp() const {
  return announced_ip_;
}

uint16_t ServerConfig::GetSignalingServerPort() const {
  return signaling_server_port_;
}

uint16_t ServerConfig::GetWebRtcMaxPort() const {
  return webrtc_max_port_;
}

uint16_t ServerConfig::GetWebRtcMinPort() const {
  return webrtc_min_port_;
}

bool ServerConfig::GetEnableGopCache() const {
  return enable_gop_cache_;
}
