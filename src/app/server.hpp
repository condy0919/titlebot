#ifndef TITLEBOT_SERVER_HPP_
#define TITLEBOT_SERVER_HPP_

#include <chrono>
#include <functional>
#include <string>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>

#include "http/fetcher.hpp"
#include "irc/irc.hpp"

namespace tb {
using Fetch = std::function<boost::asio::awaitable<FetchResult>(PrivMsg)>;

/// Connect, register, and reconnect with backoff after a disconnect or registration timeout.
/// For example, RunBot("irc.example.org", "6667", "bot", "#a,#b", fetch), when run,
/// sends "JOIN #a,#b" after the server welcomes "bot".
boost::asio::awaitable<void>
RunBot(std::string host, std::string port, std::string nick, std::string channels, Fetch fetch,
       std::chrono::milliseconds registration_timeout = std::chrono::seconds(30));

/// Serve one connected socket. On disconnect, return whether registration succeeded.
/// For example, co_await RunSession(std::move(socket), "bot", "#r", fetch) returns false
/// if the server rejects the nickname with 433 before sending the 001 welcome.
boost::asio::awaitable<bool> RunSession(boost::asio::ip::tcp::socket socket, std::string nick,
                                        std::string channels, Fetch fetch);
} // namespace tb

#endif // TITLEBOT_SERVER_HPP_
