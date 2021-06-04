#pragma once

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <cstdint>
#include <memory>

namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;

class SignalingSession : public std::enable_shared_from_this<SignalingSession> {
 public:
  // Take ownership of the stream.
  SignalingSession(tcp::socket&& socket);

  // Start the asynchronous operation.
  void Run();

 private:
  beast::tcp_stream stream_;
  beast::flat_buffer buffer_;
  http::request<http::string_body> request_;
  http::response<http::string_body> response_;

  void HandleRequest();
  void DoRead();
  void OnRead(beast::error_code ec, std::size_t bytes_transferred);
  void OnWrite(bool close,
                beast::error_code ec,
                std::size_t bytes_transferred);
  void DoClose();
};