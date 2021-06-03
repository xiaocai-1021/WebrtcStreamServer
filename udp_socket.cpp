#include "udp_socket.h"
#include "spdlog/spdlog.h"

#include <assert.h>

UdpSocket::UdpSocket(boost::asio::io_context& io_context,
                     Observer* listener,
                     size_t init_receive_buffer_size)
    : io_context_(io_context),
      is_closing_(false),
      listener_(listener),
      max_port_(65535),
      min_port_(0),
      init_receive_buffer_size_(init_receive_buffer_size) {}

UdpSocket::~UdpSocket() {
  Close();
}

bool UdpSocket::Listen(boost::string_view ip) {
  for (uint16_t i = min_port_; i <= max_port_; ++i) {
    try {
      socket_.reset(new udp::socket(
          io_context_,
          udp::endpoint(boost::asio::ip::address::from_string(ip.data()), i)));
      spdlog::debug("Select port {}.", i);
      break;
    }
    catch(...) {
      continue;
    }
  }

  if (socket_)
    StartReceive();

  if (socket_ == nullptr)
    spdlog::error("There are no ports available.");
  return socket_ != nullptr;
}

void UdpSocket::SendData(const uint8_t* buf, size_t len, udp::endpoint* endpoint) {
  UdpMessage data;
  data.buffer.reset(new uint8_t[len]);
  memcpy(data.buffer.get(), buf, len);
  data.length = len;
  data.endpoint = *endpoint;

  send_queue_.push(data);
  if (send_queue_.size() == 1)
    DoSend();
}

void UdpSocket::DoSend() {
  if (is_closing_)
    return;
  UdpMessage& data = send_queue_.front();

  boost::system::error_code ignored_error;
  socket_->async_send_to(
      boost::asio::buffer(data.buffer.get(), data.length), data.endpoint,
      boost::bind(&UdpSocket::HandSend, this, boost::asio::placeholders::error,
                  boost::asio::placeholders::bytes_transferred));
}

void UdpSocket::HandSend(const boost::system::error_code& ec,
                         size_t bytes) {
  if (is_closing_)
    return;
  if (ec) {
    if (listener_)
      listener_->OnUdpSocketError();
  }

  assert(send_queue_.size() > 0);
  send_queue_.pop();

  if (send_queue_.size() > 0)
    DoSend();
}

void UdpSocket::Close() {
  if (is_closing_)
    return;
  is_closing_ = true;

  boost::system::error_code ec;
  if (socket_) {
    socket_->shutdown(udp::socket::shutdown_both, ec);
    socket_->close();
  }
}

unsigned short UdpSocket::GetListeningPort() {
  unsigned short port = 0;
  if (socket_)
    port = socket_->local_endpoint().port();
  return port;
}

void UdpSocket::StartReceive() {
  if (!receive_data_.buffer)
    receive_data_.buffer.reset(new uint8_t[init_receive_buffer_size_]);
  assert(socket_);

  socket_->async_receive_from(
      boost::asio::buffer(receive_data_.buffer.get(),
                          init_receive_buffer_size_),
      receive_data_.endpoint,
      boost::bind(&UdpSocket::HandleReceive, this,
                  boost::asio::placeholders::error,
                  boost::asio::placeholders::bytes_transferred));
}

void UdpSocket::HandleReceive(const boost::system::error_code& ec,
                              size_t bytes) {
  if (is_closing_)
    return;
  if (!ec || ec == boost::asio::error::message_size) {
    if (listener_)
      listener_->OnUdpSocketDataReceive(receive_data_.buffer.get(), bytes,
                                 &receive_data_.endpoint);
    StartReceive();
    return;
  } else {
    if (listener_)
      listener_->OnUdpSocketError();
  }
}

void UdpSocket::SetMinMaxPort(uint16_t min, uint16_t max) {
  min_port_ = min;
  max_port_ = max;
}
