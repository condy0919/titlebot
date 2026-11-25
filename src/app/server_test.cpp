#include "server.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <sstream>

#include <boost/asio.hpp>

#include "http/fetcher.hpp"

namespace {
namespace asio = boost::asio;
using asio::use_awaitable;
using asio::ip::tcp;

// Each test owns its mock through its io_context, including during reconnects.
struct MockFetch : asio::execution_context::service {
    static inline asio::execution_context::id id; // NOLINT
    explicit MockFetch(asio::execution_context& context) : service(context) {}
    static MockFetch& For(const asio::any_io_executor& executor) {
        return asio::use_service<MockFetch>(asio::query(executor, asio::execution::context));
    }

    MOCK_METHOD(asio::awaitable<tb::FetchResult>, Fetch, (tb::PrivMsg));

    void shutdown() override {}
};
} // namespace

namespace tb {
asio::awaitable<FetchResult> FetchTitle(PrivMsg msg, asio::ssl::context&) {
    auto executor = co_await asio::this_coro::executor;
    co_return co_await MockFetch::For(executor).Fetch(std::move(msg));
}
} // namespace tb

namespace {
class Server : public testing::Test {
protected:
    void RunExercise(asio::awaitable<void> exercise, std::chrono::seconds timeout) {
        auto result = asio::co_spawn(io, std::move(exercise), asio::use_future);
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (result.wait_for(std::chrono::seconds(0)) != std::future_status::ready &&
               io.run_one_until(deadline)) {
        }
        ASSERT_EQ(result.wait_for(std::chrono::seconds(0)), std::future_status::ready);
        EXPECT_NO_THROW(result.get());
    }

    asio::io_context io;
};

asio::awaitable<std::string> ReadLine(tcp::socket& peer, std::string& input) {
    auto count =
        co_await asio::async_read_until(peer, asio::dynamic_buffer(input), "\r\n", use_awaitable);
    auto line = input.substr(0, count - 2);
    input.erase(0, count);
    co_return line;
}

asio::awaitable<void> ExerciseSession() {
    auto executor = co_await asio::this_coro::executor;
    tcp::acceptor listener(executor, {asio::ip::address_v4::loopback(), 0});
    std::vector<std::string> urls;
    asio::steady_timer release(executor);
    release.expires_at(std::chrono::steady_clock::time_point::max());
    auto fetch = [&urls, &release](tb::PrivMsg msg) -> asio::awaitable<tb::FetchResult> {
        urls.push_back(msg.url);
        if (urls.size() == 1) {
            boost::system::error_code ignored;
            co_await release.async_wait(asio::redirect_error(use_awaitable, ignored));
        }
        co_return std::string("测试标题");
    };
    EXPECT_CALL(MockFetch::For(executor), Fetch).Times(2).WillRepeatedly([&fetch](tb::PrivMsg msg) {
        return fetch(std::move(msg));
    });
    asio::co_spawn(executor,
                   tb::RunBot({
                       .server = "127.0.0.1",
                       .port = std::to_string(listener.local_endpoint().port()),
                       .nick = "title[bot]",
                       .channels = "#room",
                   }),
                   asio::detached);
    auto peer = co_await listener.async_accept(use_awaitable);
    std::string input;
    EXPECT_EQ(co_await ReadLine(peer, input), "NICK title[bot]");
    EXPECT_EQ(co_await ReadLine(peer, input), "USER title[bot] 0 * :titlebot");
    // After registration, fetch the first link and reply to the channel that sent it.
    const std::string messages = ":irc  001  title[bot] :welcome\r\n"
                                 ":alice!user@host  PRIVMSG  #room "
                                 ":http://first.example:8080/?q=title https://second.example/\r\n";
    co_await asio::async_write(peer, asio::buffer(messages), use_awaitable);
    EXPECT_EQ(co_await ReadLine(peer, input), "JOIN #room");
    // Heartbeats and private messages should still work while the first fetch is pending.
    const std::string followup = ":bob!b PRIVMSG #room :http://first.example:8080/?q=title\r\n"
                                 ":bob!b PRIVMSG #room :http://first.example:8080/?q=title\r\n"
                                 ":bob!b PRIVMSG #other :http://first.example:8080/?q=title\r\n"
                                 ":irc  PING  :keepalive\r\n"
                                 ":bob!user@host PRIVMSG Title{Bot} :https://private.example/\r\n";
    co_await asio::async_write(peer, asio::buffer(followup), use_awaitable);
    EXPECT_EQ(co_await ReadLine(peer, input), "PONG :keepalive");
    EXPECT_EQ(co_await ReadLine(peer, input), "PRIVMSG bob :测试标题");
    release.cancel();
    EXPECT_EQ(co_await ReadLine(peer, input), "PRIVMSG #room :测试标题");
    // One download serves both channels. Only two of the original channel's three subscriptions get
    // replies.
    EXPECT_EQ(co_await ReadLine(peer, input), "PRIVMSG #room :测试标题");
    EXPECT_EQ(co_await ReadLine(peer, input), "PRIVMSG #other :测试标题");
    EXPECT_EQ(urls, (std::vector<std::string>{"first.example:8080/?q=title", "private.example/"}));
}

TEST_F(Server, RepliesToFirstLinkInChannel) {
    RunExercise(ExerciseSession(), std::chrono::seconds(3));
}

asio::awaitable<void> ExerciseReconnect() {
    auto executor = co_await asio::this_coro::executor;
    tcp::acceptor listener(executor, {asio::ip::address_v4::loopback(), 0});
    asio::steady_timer release(executor, std::chrono::steady_clock::time_point::max());
    auto fetch = [&release](tb::PrivMsg msg) -> asio::awaitable<tb::FetchResult> {
        boost::system::error_code ignored;
        co_await release.async_wait(asio::redirect_error(use_awaitable, ignored));
        co_return msg.url + "\n" + std::string(413, 'x') + "中";
    };
    EXPECT_CALL(MockFetch::For(executor), Fetch).Times(3).WillRepeatedly([&fetch](tb::PrivMsg msg) {
        return fetch(std::move(msg));
    });
    asio::co_spawn(executor,
                   tb::RunBot({
                       .server = "127.0.0.1",
                       .port = std::to_string(listener.local_endpoint().port()),
                       .nick = "bot",
                       .channels = "#r",
                   }),
                   asio::detached);
    std::chrono::steady_clock::time_point disconnected;
    for (int generation = 0; generation < 2; ++generation) {
        auto peer = co_await listener.async_accept(use_awaitable);
        if (generation == 1) {
            EXPECT_GE(std::chrono::steady_clock::now() - disconnected, std::chrono::seconds(1));
        }
        std::string input;
        EXPECT_EQ(co_await ReadLine(peer, input), "NICK bot");
        EXPECT_EQ(co_await ReadLine(peer, input), "USER bot 0 * :titlebot");
        std::string messages = ":irc 001 bot :ok\r\n";
        if (generation == 0) {
            messages += ":a!b PRIVMSG #r :http://old\r\n";
        } else {
            messages += ":a!b PRIVMSG #r :http://one\r\n:a!b PRIVMSG #r :http://two\r\n";
        }
        co_await asio::async_write(peer, asio::buffer(messages), use_awaitable);
        EXPECT_EQ(co_await ReadLine(peer, input), "JOIN #r");
        std::string ping = "PING :ready\r\n";
        co_await asio::async_write(peer, asio::buffer(ping), use_awaitable);
        EXPECT_EQ(co_await ReadLine(peer, input), "PONG :ready");
        if (generation == 0) {
            // Close the first connection so its fetch remains pending across the next registration.
            disconnected = std::chrono::steady_clock::now();
            continue;
        }
        // Release the old fetch with both new ones to check reply boundaries and session isolation.
        EXPECT_EQ(release.cancel(), 3u);
        std::vector<std::string> lines;
        lines.push_back(co_await ReadLine(peer, input));
        lines.push_back(co_await ReadLine(peer, input));
        std::ranges::sort(lines);
        EXPECT_EQ(lines[0], "PRIVMSG #r :one " + std::string(413, 'x'));
        EXPECT_EQ(lines[1], "PRIVMSG #r :two " + std::string(413, 'x'));
        co_await asio::async_write(peer, asio::buffer(ping), use_awaitable);
        EXPECT_EQ(co_await ReadLine(peer, input), "PONG :ready");
    }
}

TEST_F(Server, RejoinsAndIsolatesPendingReplies) {
    RunExercise(ExerciseReconnect(), std::chrono::seconds(5));
}

TEST_F(Server, RetriesNicknameConflictsOnTheSameConnection) {
    tcp::acceptor listener(io, {asio::ip::address_v4::loopback(), 0});
    asio::co_spawn(io,
                   tb::RunBot({
                       .server = "127.0.0.1",
                       .port = std::to_string(listener.local_endpoint().port()),
                       .nick = "bot",
                       .channels = "#room",
                   }),
                   asio::detached);
    auto exercise = [&]() -> asio::awaitable<void> {
        auto peer = co_await listener.async_accept(use_awaitable);
        std::string input;
        EXPECT_EQ(co_await ReadLine(peer, input), "NICK bot");
        EXPECT_EQ(co_await ReadLine(peer, input), "USER bot 0 * :titlebot");
        std::string conflict = ":irc 433 * bot :Nickname in use\r\n";
        co_await asio::async_write(peer, asio::buffer(conflict), use_awaitable);
        auto first = co_await ReadLine(peer, input);
        EXPECT_TRUE(first.starts_with("NICK bot"));
        auto suffix = std::stoul(first.substr(8));
        EXPECT_LE(suffix, 65535u);
        EXPECT_EQ(first, "NICK bot" + std::to_string(suffix));

        conflict = ":irc 433 * " + first.substr(5) + " :Nickname in use\r\n";
        co_await asio::async_write(peer, asio::buffer(conflict), use_awaitable);
        auto next_nick = "bot" + std::to_string(suffix + 1);
        EXPECT_EQ(co_await ReadLine(peer, input), "NICK " + next_nick);
        auto welcome = ":irc 001 " + next_nick + " :Welcome\r\n";
        co_await asio::async_write(peer, asio::buffer(welcome), use_awaitable);
        EXPECT_EQ(co_await ReadLine(peer, input), "JOIN #room");
    };
    RunExercise(exercise(), std::chrono::seconds(2));
}

TEST_F(Server, FitsNicknameRetriesToTheServerLimit) {
    tcp::acceptor listener(io, {asio::ip::address_v4::loopback(), 0});
    const std::string nick = "longnickname";
    asio::co_spawn(
        io,
        tb::RunBot({"127.0.0.1", std::to_string(listener.local_endpoint().port()), nick, "#room"}),
        asio::detached);
    auto exercise = [&]() -> asio::awaitable<void> {
        for (bool reconnect : {false, true}) {
            auto peer = co_await listener.async_accept(use_awaitable);
            std::string input;
            EXPECT_EQ(co_await ReadLine(peer, input), "NICK " + nick);
            EXPECT_EQ(co_await ReadLine(peer, input), "USER " + nick + " 0 * :titlebot");
            std::string conflict = ":irc 433 * " + nick + " :In use\r\n";
            co_await asio::async_write(peer, asio::buffer(conflict), use_awaitable);
            auto retry = co_await ReadLine(peer, input);
            EXPECT_EQ(retry.size(), 5u + (reconnect ? 2u : 9u));
            EXPECT_TRUE(retry.starts_with("NICK l"));
            if (reconnect) {
                std::string seen;
                for (int attempt = 0; attempt < 10; ++attempt) {
                    EXPECT_TRUE(retry.starts_with("NICK l"));
                    EXPECT_EQ(retry.size(), 7u);
                    EXPECT_GE(retry.back(), '0');
                    EXPECT_LE(retry.back(), '9');
                    EXPECT_EQ(seen.find(retry.back()), seen.npos);
                    seen.push_back(retry.back());
                    conflict = ":irc 433 * " + retry.substr(5) + " :In use\r\n";
                    co_await asio::async_write(peer, asio::buffer(conflict), use_awaitable);
                    if (attempt < 9) {
                        retry = co_await ReadLine(peer, input);
                    }
                }
                const std::string ping = "PING :done\r\n";
                co_await asio::async_write(peer, asio::buffer(ping), use_awaitable);
                EXPECT_EQ(co_await ReadLine(peer, input), "PONG :done");
            } else {
                // A server can truncate the rejected NICK before it sends ISUPPORT.
                auto truncated = retry.substr(5, 4);
                conflict = ":irc 433 * " + truncated + " :In use\r\n";
                co_await asio::async_write(peer, asio::buffer(conflict), use_awaitable);
                retry = co_await ReadLine(peer, input);
                EXPECT_EQ(retry.size(), 9u);
                // Servers normally send ISUPPORT after registration. Use its limit on reconnect.
                auto welcome = ":irc 001 " + retry.substr(5) +
                               " :Welcome\r\n"
                               ":irc 005 bot PREFIX=(ov)@+ NICKLEN=2 :are supported\r\n"
                               ":irc 005 bot NICKLEN=0 NICKLEN=2x :NICKLEN=30 ignored\r\n"
                               "PING :ready\r\n";
                co_await asio::async_write(peer, asio::buffer(welcome), use_awaitable);
                EXPECT_EQ(co_await ReadLine(peer, input), "JOIN #room");
                EXPECT_EQ(co_await ReadLine(peer, input), "PONG :ready");
            }
        }
    };
    RunExercise(exercise(), std::chrono::seconds(5));
}

TEST_F(Server, LogsServerErrorsKicksAndJoinFailures) {
    std::ostringstream output;
    auto previous = spdlog::default_logger();
    auto logger = std::make_shared<spdlog::logger>(
        "irc-events", std::make_shared<spdlog::sinks::ostream_sink_st>(output));
    logger->set_pattern("%l %v");
    spdlog::set_default_logger(logger);

    tcp::acceptor listener(io, {asio::ip::address_v4::loopback(), 0});
    asio::co_spawn(
        io,
        tb::RunBot({"127.0.0.1", std::to_string(listener.local_endpoint().port()), "bot", "#room"}),
        asio::detached);
    auto exercise = [&]() -> asio::awaitable<void> {
        auto peer = co_await listener.async_accept(use_awaitable);
        std::string input;
        EXPECT_EQ(co_await ReadLine(peer, input), "NICK bot");
        EXPECT_EQ(co_await ReadLine(peer, input), "USER bot 0 * :titlebot");
        std::string events = ":irc 437 * bot :Nickname unavailable\r\n"
                             ":irc 471 bot #room :Channel full\r\n"
                             ":irc 473 bot #room :Invite only\r\n"
                             ":irc 474 bot #room :Banned\r\n"
                             ":irc 475 bot #room :Bad key\r\n"
                             ":op!u@h KICK #room BOT :reason with spaces\r\n"
                             ":op!u@h KICK #room alice :rules\r\n"
                             "ERROR :Closing link\r\n"
                             "PING :done\r\n";
        co_await asio::async_write(peer, asio::buffer(events), use_awaitable);
        EXPECT_EQ(co_await ReadLine(peer, input), "PONG :done");
    };
    RunExercise(exercise(), std::chrono::seconds(3));
    spdlog::set_default_logger(previous);
    EXPECT_THAT(
        output.str(),
        testing::HasSubstr("warning IRC 437: bot temporarily unavailable: Nickname unavailable\n"
                           "warning IRC JOIN #room failed (471): Channel full\n"
                           "warning IRC JOIN #room failed (473): Invite only\n"
                           "warning IRC JOIN #room failed (474): Banned\n"
                           "warning IRC JOIN #room failed (475): Bad key\n"
                           "warning IRC KICK: op!u@h removed us from #room: reason with spaces\n"
                           "info IRC KICK: op!u@h removed alice from #room: rules\n"
                           "error IRC ERROR: Closing link\n"));
}

TEST_F(Server, ProbesIdleSessionsAndClosesUnresponsivePeers) {
    tcp::acceptor listener(io, {asio::ip::address_v4::loopback(), 0});
    tb::BotConfig config{"127.0.0.1", std::to_string(listener.local_endpoint().port()), "bot",
                         "#r"};
    config.idle_timeout = std::chrono::milliseconds(40);
    config.ping_timeout = std::chrono::milliseconds(40);
    asio::co_spawn(io, tb::RunBot(config), asio::detached);
    auto exercise = [&]() -> asio::awaitable<void> {
        auto peer = co_await listener.async_accept(use_awaitable);
        std::string input;
        co_await ReadLine(peer, input);
        co_await ReadLine(peer, input);
        co_await asio::async_write(peer, asio::buffer(std::string_view(":irc 001 bot :ok\r\n")),
                                   use_awaitable);
        EXPECT_EQ(co_await ReadLine(peer, input), "JOIN #r");
        EXPECT_EQ(co_await ReadLine(peer, input), "PING :titlebot");
        co_await asio::async_write(peer, asio::buffer(std::string_view("PONG :titlebot\r\n")),
                                   use_awaitable);
        EXPECT_EQ(co_await ReadLine(peer, input), "PING :titlebot");
        char c;
        boost::system::error_code ec;
        co_await peer.async_read_some(asio::buffer(&c, 1), asio::redirect_error(use_awaitable, ec));
        EXPECT_EQ(ec, asio::error::eof);
    };
    RunExercise(exercise(), std::chrono::seconds(3));
}

TEST_F(Server, ReconnectsAfterRegistrationDeadline) {
    tcp::acceptor listener(io, {asio::ip::address_v4::loopback(), 0});
    asio::co_spawn(io,
                   tb::RunBot({
                       .server = "127.0.0.1",
                       .port = std::to_string(listener.local_endpoint().port()),
                       .nick = "bot",
                       .channels = "#r",
                       .registration_timeout = std::chrono::milliseconds(150),
                   }),
                   asio::detached);
    auto exercise = [&]() -> asio::awaitable<void> {
        for (bool register_now : {false, true}) {
            auto peer = co_await listener.async_accept(use_awaitable);
            std::string input;
            EXPECT_EQ(co_await ReadLine(peer, input), "NICK bot");
            EXPECT_EQ(co_await ReadLine(peer, input), "USER bot 0 * :titlebot");
            if (register_now) {
                std::string welcome = ":irc 001 bot :welcome\r\n";
                co_await asio::async_write(peer, asio::buffer(welcome), use_awaitable);
                EXPECT_EQ(co_await ReadLine(peer, input), "JOIN #r");
                asio::steady_timer wait(io, std::chrono::milliseconds(200));
                co_await wait.async_wait(use_awaitable);
            }
            std::string ping = "PING :test\r\n";
            co_await asio::async_write(peer, asio::buffer(ping), use_awaitable);
            EXPECT_EQ(co_await ReadLine(peer, input), "PONG :test");
            if (!register_now) {
                char byte;
                boost::system::error_code ec;
                co_await peer.async_read_some(asio::buffer(&byte, 1),
                                              asio::redirect_error(use_awaitable, ec));
                EXPECT_EQ(ec, asio::error::eof);
            }
        }
    };
    RunExercise(exercise(), std::chrono::seconds(3));
}
} // namespace
