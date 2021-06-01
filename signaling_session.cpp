#include "signaling_session.h"

// Take ownership of the stream
SignalingSession::SignalingSession(tcp::socket&& socket)
    : stream_(std::move(socket)), lambda_(*this) {}

// Start the asynchronous operation
void SignalingSession::run() {
  net::dispatch(stream_.get_executor(),
                beast::bind_front_handler(&SignalingSession::do_read,
                                          shared_from_this()));
}

void SignalingSession::do_read() {
  // Make the request empty before reading,
  // otherwise the operation behavior is undefined.
  req_ = {};

  // Set the timeout.
  stream_.expires_after(std::chrono::seconds(30));

  // Read a request
  http::async_read(stream_, buffer_, req_,
                   beast::bind_front_handler(&SignalingSession::on_read,
                                             shared_from_this()));
}

void SignalingSession::on_read(beast::error_code ec,
                               std::size_t bytes_transferred) {
  boost::ignore_unused(bytes_transferred);

  // This means they closed the connection
  if (ec == http::error::end_of_stream)
    return do_close();

  if (ec) {
    spdlog::error("Signal session read failed. err = {}", ec.message());
    return;
  }

  // Send the response
  handle_request(std::move(req_), lambda_);
}

void SignalingSession::on_write(bool close,
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
    return do_close();
  }

  // We're done with the response so delete it
  res_ = nullptr;

  // Read another request
  do_read();
}

void SignalingSession::do_close() {
  // Send a TCP shutdown
  beast::error_code ec;
  stream_.socket().shutdown(tcp::socket::shutdown_send, ec);

  // At this point the connection is closed gracefully
}