#ifndef IRCBOT_TCP_TRANSPORT_HPP_
#define IRCBOT_TCP_TRANSPORT_HPP_

#include <memory>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ssl/context.hpp>

namespace ircbot {
struct Config;
class Transport;
} // namespace ircbot

namespace ircbot::detail {
/// For example, ConnectTcp(config, nullptr) connects a plain TCP transport to config.server.
boost::asio::awaitable<std::unique_ptr<Transport>> ConnectTcp(const Config& config,
                                                              boost::asio::ssl::context* tls);
} // namespace ircbot::detail
#endif // IRCBOT_TCP_TRANSPORT_HPP_
