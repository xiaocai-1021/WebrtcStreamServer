#pragma once

#include <boost/asio.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/config.hpp>
#include <cstdint>
#include <memory>

#include "media_source_manager.h"
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"
#include "webrtc_transport.h"
#include "webrtc_transport_manager.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

// This function produces an HTTP response for the given
// request. The type of the response object depends on the
// contents of the request, so the interface requires the
// caller to pass a generic lambda for receiving the response.
template <class Body, class Allocator, class Send>
void handle_request(http::request<Body, http::basic_fields<Allocator>>&& req,
                    Send&& send) {
  http::response<http::string_body> res;
  nlohmann::json response_json;

  if (req.target() == "/play") {
    if (req.method() == http::verb::post) {
      nlohmann::json json = nlohmann::json::parse(req.body());
      auto media_source =
          MediaSourceManager::GetInstance().Query(json["streamId"]);

      if (media_source) {
        auto webrtc_transport =
            std::make_shared<WebrtcTransport>(json["streamId"]);
        webrtc_transport->SetOffer(json["offer"]);
        if (webrtc_transport->Start()) {
          media_source->RegisterObserver(webrtc_transport.get());
          auto sdp = webrtc_transport->CreateAnswer();
          WebrtcTransportManager::GetInstance().Add(webrtc_transport);
          response_json["error"] = false;
          response_json["answer"] = sdp;
        } else {
          response_json["error"] = true;
        }
      } else {
        response_json["error"] = true;
      }
    }
  } else if (req.target().starts_with("/streams")) {
    if (req.method() == http::verb::get)
      response_json = MediaSourceManager::GetInstance().List();
    else if (req.method() == http::verb::post) {
      nlohmann::json req_json = nlohmann::json::parse(req.body());
      std::string url = req_json["url"];

      auto result = MediaSourceManager::GetInstance().Add(url);
      if (result) {
        response_json["error"] = false;
        response_json["id"] = *result;
      } else {
        response_json["error"] = true;
      }
    } else if (req.method() == http::verb::delete_) {
      auto target = req.target();
      auto pos = target.find("/streams/");
      if (pos == std::string::npos) {
        response_json["error"] = true;
      } else {
        auto id = target.substr(pos + strlen("/streams/"));
        MediaSourceManager::GetInstance().Remove({id.data(), id.size()});
      }
    } else {
      response_json["error"] = true;
    }
  } else {
    response_json["error"] = true;
  }

  res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
  res.set(http::field::content_type, "text/plain");
  res.set(http::field::access_control_allow_origin, "*");
  res.keep_alive(req.keep_alive());
  res.body() = response_json.dump();
  send(std::move(res));
}

// Handles an HTTP server connection.
class SignalingSession : public std::enable_shared_from_this<SignalingSession> {
  // This is the C++11 equivalent of a generic lambda.
  // The function object is used to send an HTTP message.
  struct send_lambda {
    SignalingSession& self_;

    explicit send_lambda(SignalingSession& self) : self_(self) {}
    template <bool isRequest, class Body, class Fields>
    void operator()(http::message<isRequest, Body, Fields>&& msg) const {
      // The lifetime of the message has to extend
      // for the duration of the async operation so
      // we use a shared_ptr to manage it.
      auto sp = std::make_shared<http::message<isRequest, Body, Fields>>(
          std::move(msg));

      // Store a type-erased version of the shared
      // pointer in the class to keep it alive.
      self_.res_ = sp;

      // Write the response.
      http::async_write(
          self_.stream_, *sp,
          beast::bind_front_handler(&SignalingSession::on_write,
                                    self_.shared_from_this(), sp->need_eof()));
    }
  };

  beast::tcp_stream stream_;
  beast::flat_buffer buffer_;
  http::request<http::string_body> req_;
  std::shared_ptr<void> res_;
  send_lambda lambda_;

 public:
  // Take ownership of the stream.
  SignalingSession(tcp::socket&& socket);

  // Start the asynchronous operation.
  void run();

 private:
  void do_read();
  void on_read(beast::error_code ec, std::size_t bytes_transferred);
  void on_write(bool close,
                beast::error_code ec,
                std::size_t bytes_transferred);
  void do_close();
};