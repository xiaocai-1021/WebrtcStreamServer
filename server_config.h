#pragma once

#include <cstdint>
#include <string>
#include <boost/utility/string_view.hpp>

class ServerConfig {
 public:
  static ServerConfig& GetInstance();
  bool Load(boost::string_view json_file_name);
  const std::string& GetIp() const;
  const std::string& GetAnnouncedIp() const;
  uint16_t GetSignalingServerPort() const;
  uint16_t GetWebRtcMaxPort() const;
  uint16_t GetWebRtcMinPort() const;
  bool GetEnableGopCache() const;

 private:
  ServerConfig() = default;
  std::string ip_;
  std::string announced_ip_;
  uint16_t signaling_server_port_;
  uint16_t webrtc_max_port_;
  uint16_t webrtc_min_port_;
  bool enable_gop_cache_;
};