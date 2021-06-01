#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

#include "webrtc_transport.h"

/**
 * @brief Manage all webrtc transports.
 *
 */
class WebrtcTransportManager : public WebrtcTransport::Observer {
 public:
  static WebrtcTransportManager& GetInstance();

  void Start();
  void Stop();
  void Add(std::shared_ptr<WebrtcTransport> webrtc_transport);

 private:
  void Work();
  void OnWebrtcTransportShutdown(
      std::shared_ptr<WebrtcTransport> webrtc_transport) override;
  std::unordered_set<std::shared_ptr<WebrtcTransport>> webrtc_transports_;
  std::thread work_thread_;
  std::mutex mutex_;
  std::condition_variable idle_cond_;
  std::condition_variable busy_cond_;
  std::atomic<bool> closed_{false};
  std::shared_ptr<WebrtcTransport> will_be_deleted_;
};