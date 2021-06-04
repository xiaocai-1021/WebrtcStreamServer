#include "signaling_session.h"

#include "media_source_manager.h"
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"
#include "webrtc_transport.h"
#include "webrtc_transport_manager.h"

void SignalingSession::HandleRequest() {
  nlohmann::json response_json;

  if (request_.target() == "/play") {
    if (request_.method() == http::verb::post) {
      nlohmann::json json = nlohmann::json::parse(request_.body());
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
  } else if (request_.target().starts_with("/streams")) {
    if (request_.method() == http::verb::get)
      response_json = MediaSourceManager::GetInstance().List();
    else if (request_.method() == http::verb::post) {
      nlohmann::json req_json = nlohmann::json::parse(request_.body());
      std::string url = req_json["url"];

      auto result = MediaSourceManager::GetInstance().Add(url);
      if (result) {
        response_json["error"] = false;
        response_json["id"] = *result;
      } else {
        response_json["error"] = true;
      }
    } else if (request_.method() == http::verb::delete_) {
      auto target = request_.target();
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

  response_.set(http::field::server, BOOST_BEAST_VERSION_STRING);
  response_.set(http::field::content_type, "text/plain");
  response_.set(http::field::access_control_allow_origin, "*");
  response_.keep_alive(request_.keep_alive());
  response_.body() = response_json.dump();

  http::async_write(
    stream_, response_,
    beast::bind_front_handler(&SignalingSession::OnWrite,
                              shared_from_this(), response_.need_eof()));
}

// Take ownership of the stream
SignalingSession::SignalingSession(tcp::socket&& socket)
    : stream_(std::move(socket)) {}

// Start the asynchronous operation
void SignalingSession::Run() {
  DoRead();
}

void SignalingSession::DoRead() {
  // Make the request empty before reading,
  // otherwise the operation behavior is undefined.
  request_ = {};

  // Set the timeout.
  stream_.expires_after(std::chrono::seconds(30));

  // Read a request
  http::async_read(stream_, buffer_, request_,
                   beast::bind_front_handler(&SignalingSession::OnRead,
                                             shared_from_this()));
}

void SignalingSession::OnRead(beast::error_code ec,
                               std::size_t bytes_transferred) {
  boost::ignore_unused(bytes_transferred);

  // This means they closed the connection
  if (ec == http::error::end_of_stream)
    return DoClose();

  if (ec) {
    spdlog::error("Signal session read failed. err = {}", ec.message());
    return;
  }

  // Send the response
  HandleRequest();
}

void SignalingSession::OnWrite(bool close,
                                beast::error_code ec,
                                std::size_t bytes_transferred) {
  boost::ignore_unused(bytes_transferred);

  if (ec) {
    spdlog::error("Signaling session write failed. err = {}", ec.message());
    return;
  }

  if (close) {
    // This means we should close the connection, usually because
    // the response indicated the "Connection: close" semantic.
    return DoClose();
  }

  // Read another request
  DoRead();
}

void SignalingSession::DoClose() {
  // Send a TCP shutdown
  beast::error_code ec;
  stream_.socket().shutdown(tcp::socket::shutdown_send, ec);

  // At this point the connection is closed gracefully
}