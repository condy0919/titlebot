#ifndef TITLEBOT_SERVER_HPP_
#define TITLEBOT_SERVER_HPP_

#include <chrono>
#include <string>

#include <boost/asio/awaitable.hpp>

namespace tb {
struct BotConfig {
    std::string server;
    std::string port;
    std::string nick;
    std::string channels;
    bool tls = false;
    std::chrono::milliseconds registration_timeout = std::chrono::seconds(30);
    std::chrono::milliseconds connect_timeout = std::chrono::seconds(15);
    std::chrono::milliseconds idle_timeout = std::chrono::seconds(90);
    std::chrono::milliseconds ping_timeout = std::chrono::seconds(30);
    std::chrono::milliseconds stable_session = std::chrono::minutes(1);
};

/// Connect, register, and reconnect with backoff after a disconnect or registration timeout.
/// For example, RunBot({"irc.example.org", "6667", "bot", "#a,#b"}) joins both channels
/// after the server welcomes "bot".
boost::asio::awaitable<void> RunBot(BotConfig config);

} // namespace tb

#endif // TITLEBOT_SERVER_HPP_
