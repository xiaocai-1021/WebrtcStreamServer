#include "webrtc_transport_manager.h"

#include "spdlog/spdlog.h"

WebrtcTransportManager& WebrtcTransportManager::GetInstance() {
  static WebrtcTransportManager webrtc_tranport_manager;
  return webrtc_tranport_manager;
}

void WebrtcTransportManager::Start() {
  work_thread_ = std::thread(&WebrtcTransportManager::Work, this);
}

void WebrtcTransportManager::Stop() {
  closed_ = true;
  busy_cond_.notify_one();
  idle_cond_.notify_one();
  if (work_thread_.joinable())
    work_thread_.join();
  for (auto iter = webrtc_transports_.begin();
       iter != webrtc_transports_.end();) {
    (*iter)->DeregisterObserver();
    (*iter)->Stop();
    webrtc_transports_.erase(iter++);
  }
}

void WebrtcTransportManager::Work() {
  while (!closed_) {
    std::unique_lock<std::mutex> guard(mutex_);
    busy_cond_.wait(guard, [this] { return closed_ || will_be_deleted_; });
    auto result = webrtc_transports_.find(will_be_deleted_);
    if (result != webrtc_transports_.end()) {
      (*result)->DeregisterObserver();
      (*result)->Stop();
      webrtc_transports_.erase(result);
      spdlog::debug(
          "Now there are {} [WebrtcTransport] in [WebrtcTransportManager].",
          webrtc_transports_.size());
    } else {
      if (!closed_)
        spdlog::warn("The [WebrtcTransport] to be deleted is not in set.");
    }
    will_be_deleted_.reset();
    idle_cond_.notify_one();
  }
}

void WebrtcTransportManager::Add(
    std::shared_ptr<WebrtcTransport> webrtc_transport) {
  std::lock_guard<std::mutex> guard(mutex_);
  webrtc_transport->RegisterObserver(this);
  webrtc_transports_.insert(webrtc_transport);
}

void WebrtcTransportManager::OnWebrtcTransportShutdown(
    std::shared_ptr<WebrtcTransport> webrtc_transport) {
  std::unique_lock<std::mutex> guard(mutex_);
  idle_cond_.wait(guard, [this] { return closed_ || !will_be_deleted_; });
  will_be_deleted_ = webrtc_transport;
  busy_cond_.notify_one();
}