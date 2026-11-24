#include "server.hpp"

#include <gtest/gtest.h>

#include <boost/asio.hpp>

namespace {
namespace asio = boost::asio;
using asio::use_awaitable;
using asio::ip::tcp;

class Server : public testing::Test {
protected:
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
    tcp::socket bot(executor);
    co_await bot.async_connect(listener.local_endpoint(), use_awaitable);
    auto peer = co_await listener.async_accept(use_awaitable);
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
    asio::co_spawn(executor, tb::RunSession(std::move(bot), "titlebot", "#room", fetch),
                   asio::detached);
    std::string input;
    EXPECT_EQ(co_await ReadLine(peer, input), "NICK titlebot");
    EXPECT_EQ(co_await ReadLine(peer, input), "USER titlebot 0 * :titlebot");
    // After registration, fetch the first link and reply to the channel that sent it.
    const std::string messages = ":irc  001  titlebot :welcome\r\n"
                                 ":alice!user@host  PRIVMSG  #room "
                                 ":http://first.example:8080/?q=title https://second.example/\r\n";
    co_await asio::async_write(peer, asio::buffer(messages), use_awaitable);
    EXPECT_EQ(co_await ReadLine(peer, input), "JOIN #room");
    // Heartbeats and private messages should still work while the first fetch is pending.
    const std::string followup = ":bob!b PRIVMSG #room :http://first.example:8080/?q=title\r\n"
                                 ":bob!b PRIVMSG #room :http://first.example:8080/?q=title\r\n"
                                 ":bob!b PRIVMSG #other :http://first.example:8080/?q=title\r\n"
                                 ":irc  PING  :keepalive\r\n"
                                 ":bob!user@host PRIVMSG TitleBot :https://private.example/\r\n";
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
    auto test = asio::co_spawn(io, ExerciseSession(), asio::use_future);
    io.run_for(std::chrono::seconds(3));
    ASSERT_EQ(test.wait_for(std::chrono::seconds(0)), std::future_status::ready);
    EXPECT_NO_THROW(test.get());
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
    asio::co_spawn(executor,
                   tb::RunBot("127.0.0.1", std::to_string(listener.local_endpoint().port()), "bot",
                              "#r", fetch),
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
        std::sort(lines.begin(), lines.end());
        EXPECT_EQ(lines[0], "PRIVMSG #r :one " + std::string(413, 'x'));
        EXPECT_EQ(lines[1], "PRIVMSG #r :two " + std::string(413, 'x'));
        co_await asio::async_write(peer, asio::buffer(ping), use_awaitable);
        EXPECT_EQ(co_await ReadLine(peer, input), "PONG :ready");
    }
}

TEST_F(Server, RejoinsAndIsolatesPendingReplies) {
    std::exception_ptr error;
    bool completed = false;
    asio::co_spawn(io, ExerciseReconnect(), [&](std::exception_ptr result) {
        error = result;
        completed = true;
        io.stop();
    });
    io.run_for(std::chrono::seconds(5));
    ASSERT_TRUE(completed);
    if (error) {
        std::rethrow_exception(error);
    }
}

TEST_F(Server, EndsUnregisteredSessionOnNicknameConflict) {
    tcp::acceptor listener(io, {asio::ip::address_v4::loopback(), 0});
    tcp::socket client(io);
    client.connect(listener.local_endpoint());
    auto peer = listener.accept();
    auto result =
        asio::co_spawn(io, tb::RunSession(std::move(client), "bot", "#room", {}), asio::use_future);
    std::string conflict = ":irc  433  * bot :Nickname in use\r\n";
    asio::write(peer, asio::buffer(conflict));
    io.run_for(std::chrono::seconds(2));
    ASSERT_EQ(result.wait_for(std::chrono::seconds(0)), std::future_status::ready);
    EXPECT_FALSE(result.get());
}

TEST_F(Server, ReconnectsAfterRegistrationDeadline) {
    tcp::acceptor listener(io, {asio::ip::address_v4::loopback(), 0});
    asio::co_spawn(io,
                   tb::RunBot("127.0.0.1", std::to_string(listener.local_endpoint().port()), "bot",
                              "#r", {}, std::chrono::milliseconds(150)),
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
    auto result = asio::co_spawn(io, exercise(), asio::use_future);
    io.run_for(std::chrono::seconds(3));
    ASSERT_EQ(result.wait_for(std::chrono::seconds(0)), std::future_status::ready);
    EXPECT_NO_THROW(result.get());
}
} // namespace
