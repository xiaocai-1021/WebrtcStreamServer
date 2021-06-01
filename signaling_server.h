#include <cstdint>
#include <memory>
#include <boost/utility/string_view.hpp>
#include <boost/asio.hpp>

namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;
// Accepts incoming connections and launches the sessions.
class SignalingServer : public std::enable_shared_from_this<SignalingServer> {
 public:
  SignalingServer(boost::asio::io_context& io_context);
  bool Start(boost::string_view ip, uint16_t port);

 private:
  void DoAccept();
  void OnAccept(boost::system::error_code ec, tcp::socket socket);
  net::io_context& ioc_;
  tcp::acceptor acceptor_;
};