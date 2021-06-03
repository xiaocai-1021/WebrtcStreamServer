#include "webrtc_transport_manager.h"

#include "spdlog/spdlog.h"

WebrtcTransportManager::WebrtcTransportManager() : work_guard_(message_loop_.get_executor()) {

}

WebrtcTransportManager& WebrtcTransportManager::GetInstance() {
  static WebrtcTransportManager webrtc_tranport_manager;
  return webrtc_tranport_manager;
}

void WebrtcTransportManager::Start() {
  if (work_thread_.get_id() == std::thread::id())
    work_thread_ = std::thread(boost::bind(&boost::asio::io_context::run, &message_loop_));
}

void WebrtcTransportManager::Add(std::shared_ptr<WebrtcTransport> webrtc_transport) {
  message_loop_.post([webrtc_transport, this]() {
    webrtc_transports_.insert(webrtc_transport);
  });
}

void WebrtcTransportManager::Remove(std::shared_ptr<WebrtcTransport> webrtc_transport) {
  message_loop_.post([webrtc_transport, this]() {
    auto result = webrtc_transports_.find(webrtc_transport);
    if (result != webrtc_transports_.end()) {
      (*result)->Stop();
      webrtc_transports_.erase(result);
      spdlog::debug(
          "Now there are {} [WebrtcTransport] in [WebrtcTransportManager].",
          webrtc_transports_.size());
    } else {
      spdlog::warn("The [WebrtcTransport] to be deleted is not in set.");
    }
  });
}

void WebrtcTransportManager::Stop() {
  work_guard_.reset();
  if (work_thread_.joinable())
    work_thread_.join();
}