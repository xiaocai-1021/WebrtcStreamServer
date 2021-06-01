#include <sys/resource.h>

#include <iostream>
#include <vector>

#include "boost/asio.hpp"
#include "dtls_context.h"
#include "hmac_sha1.h"
#include "media_source_manager.h"
#include "server_config.h"
#include "signaling_server.h"
#include "spdlog/spdlog.h"
#include "srtp_session.h"
#include "webrtc_transport_manager.h"

int main(int argc, char* argv[]) {
#ifdef NDEBUG
  spdlog::set_level(spdlog::level::info);
#else
  rlimit l = {RLIM_INFINITY, RLIM_INFINITY};
  setrlimit(RLIMIT_CORE, &l);
  spdlog::set_level(spdlog::level::debug);
#endif

  if (!ServerConfig::GetInstance().Load("../config.json")) {
    spdlog::error("Failed to load config file.");
    return EXIT_FAILURE;
  }

  WebrtcTransportManager::GetInstance().Start();

  if (!DtlsContext::GetInstance().Initialize()) {
    spdlog::error("Failed to initialize dtls.");
    return EXIT_FAILURE;
  }
  
  if (!LibSrtpInitializer::GetInstance().Initialize()) {
    spdlog::error("Failed to initialize libsrtp.");
    return EXIT_FAILURE;
  }

  boost::asio::io_context ioc;
  std::shared_ptr<SignalingServer> server =
      std::make_shared<SignalingServer>(ioc);
  if (!server->Start(ServerConfig::GetInstance().GetIp(),
                     ServerConfig::GetInstance().GetSignalingServerPort())) {
    spdlog::error("Signaling server failed to start.");
    return EXIT_FAILURE;
  }

  boost::asio::signal_set signals(ioc, SIGINT);
  signals.async_wait(
      [&](const boost::system::error_code& error, int signal_number) {
        if (signal_number == SIGINT && !error) {
          ioc.stop();
          MediaSourceManager::GetInstance().StopAll();
          WebrtcTransportManager::GetInstance().Stop();
        }
      });
  ioc.run();

  return EXIT_SUCCESS;
}