#include "server_config.h"

#include <fstream>

#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

ServerConfig& ServerConfig::GetInstance() {
  static ServerConfig server_config;
  return server_config;
}

bool ServerConfig::Load(boost::string_view json_file_name) {
  std::ifstream config_file(json_file_name.data());
  if (!config_file.is_open()) {
    spdlog::error("Open config file {} failed.", json_file_name.data());
    return false;
  }

  try {
    nlohmann::json json;
    config_file >> json;

    ip_ = json["ip"];
    announced_ip_ = json["announced_ip"];
    signaling_server_port_ = json["signaling_server_port"];
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