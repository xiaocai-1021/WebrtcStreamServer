#pragma once

#include <memory>
#include <thread>
#include <unordered_set>
#include <boost/asio.hpp>

#include "webrtc_transport.h"

/**
 * @brief Manage all webrtc transports.
 *
 */
class WebrtcTransportManager {
 public:
  static WebrtcTransportManager& GetInstance();

  void Start();
  void Stop();
  void Add(std::shared_ptr<WebrtcTransport> webrtc_transport);
  void Remove(std::shared_ptr<WebrtcTransport> webrtc_transport);

 private:
  WebrtcTransportManager();
  std::unordered_set<std::shared_ptr<WebrtcTransport>> webrtc_transports_;
  boost::asio::io_context message_loop_;
  using work_guard_type = boost::asio::executor_work_guard<boost::asio::io_context::executor_type>;
  work_guard_type work_guard_;
  std::thread work_thread_;
};