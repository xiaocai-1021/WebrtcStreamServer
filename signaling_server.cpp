#include "signaling_server.h"

#include "boost/beast.hpp"
#include "signaling_session.h"
#include "spdlog/spdlog.h"

SignalingServer::SignalingServer(boost::asio::io_context& io_context)
    : ioc_{io_context}, acceptor_(net::make_strand(ioc_)) {}

bool SignalingServer::Start(boost::string_view ip, uint16_t port) {
  boost::system::error_code ec;
  auto const address = net::ip::make_address(ip.data());
  auto endpoint = tcp::endpoint{address, port};

  acceptor_.open(endpoint.protocol(), ec);
  if (ec) {
    spdlog::error("Acceptor open failed. err = {}", ec.message());
    return false;
  }

  acceptor_.set_option(net::socket_base::reuse_address(true), ec);
  if (ec) {
    spdlog::error("Acceptor set opion [reuse_address] failed. err = {}",
                  ec.message());
    return false;
  }

  acceptor_.bind(endpoint, ec);
  if (ec) {
    spdlog::error("Acceptor bind failed. err = {}", ec.message());
    return false;
  }

  acceptor_.listen(net::socket_base::max_listen_connections, ec);
  if (ec) {
    spdlog::error("Acceptor listen failed. err = {}", ec.message());
    return false;
  }

  DoAccept();
  return true;
}

void SignalingServer::DoAccept() {
  acceptor_.async_accept(std::bind(&SignalingServer::OnAccept,
                                   shared_from_this(), std::placeholders::_1,
                                   std::placeholders::_2));
}

void SignalingServer::OnAccept(boost::system::error_code ec,
                                tcp::socket socket) {
  if (ec) {
    spdlog::error("Signaling server accept failed. ec = {}", ec.message());
  } else {
    std::make_shared<SignalingSession>(std::move(socket))->run();
  }

  DoAccept();
}