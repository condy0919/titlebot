#ifndef TITLEBOT_SERVER_HPP_
#define TITLEBOT_SERVER_HPP_

#include <chrono>
#include <functional>
#include <string>

#include <boost/asio/awaitable.hpp>

#include "common/runtime_limits.hpp"
#include "http/fetcher.hpp"

namespace tb {
struct SessionTimeouts {
    std::chrono::milliseconds registration_timeout = std::chrono::seconds(30);
    std::chrono::milliseconds connect_timeout = std::chrono::seconds(15);
    std::chrono::milliseconds idle_timeout = std::chrono::seconds(90);
    std::chrono::milliseconds ping_timeout = std::chrono::seconds(30);
    std::chrono::milliseconds stable_session = std::chrono::minutes(1);
};

struct BotConfig {
    std::string server;
    std::string port;
    std::string nick;
    std::string channels;
    bool tls = false;
    FetchLimits fetch{};
    SchedulerLimits scheduler{};
    ReplyLimits replies{};
    SessionTimeouts timeouts{};
};

/// Connect, register, and reconnect with backoff after a disconnect or registration timeout.
/// For example, RunBot({"irc.example.org", "6667", "bot", "#a,#b"}) joins both channels
/// after the server welcomes "bot".
boost::asio::awaitable<void> RunBot(BotConfig config);

using TitleFetcher = std::function<boost::asio::awaitable<FetchResult>(PrivMsg)>;
/// Use fetch for each scheduled URL. For example, an offline fetcher may return a fixture title.
boost::asio::awaitable<void> RunBot(BotConfig config, TitleFetcher fetch);

} // namespace tb

#endif // TITLEBOT_SERVER_HPP_
