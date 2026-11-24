#include "server.hpp"
#include <boost/asio.hpp>
#include <gtest/gtest.h>

namespace {
namespace asio = boost::asio;
using asio::use_awaitable;
using asio::ip::tcp;

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
    auto read_line = [&]() -> asio::awaitable<std::string> {
        auto count = co_await asio::async_read_until(peer, asio::dynamic_buffer(input), "\r\n",
                                                     use_awaitable);
        auto line = input.substr(0, count - 2);
        input.erase(0, count);
        co_return line;
    };
    EXPECT_EQ(co_await read_line(), "NICK titlebot");
    EXPECT_EQ(co_await read_line(), "USER titlebot 0 * :titlebot");
    // 注册成功后模拟频道消息，验证只抓取首个链接且回到原频道。
    const std::string messages = ":irc 001 titlebot :welcome\r\n"
                                 ":alice!user@host PRIVMSG #room "
                                 ":http://first.example:8080/?q=title https://second.example/\r\n";
    co_await asio::async_write(peer, asio::buffer(messages), use_awaitable);
    EXPECT_EQ(co_await read_line(), "JOIN #room");
    // 首次抓取仍在等待时，心跳和私聊应能独立完成。
    const std::string followup = "PING :keepalive\r\n"
                                 ":bob!user@host PRIVMSG titlebot :https://private.example/\r\n";
    co_await asio::async_write(peer, asio::buffer(followup), use_awaitable);
    EXPECT_EQ(co_await read_line(), "PONG :keepalive");
    EXPECT_EQ(co_await read_line(), "PRIVMSG bob :测试标题");
    release.cancel();
    EXPECT_EQ(co_await read_line(), "PRIVMSG #room :测试标题");
    EXPECT_EQ(urls, (std::vector<std::string>{"first.example:8080/?q=title", "private.example/"}));
}

TEST(Server, RepliesToFirstLinkInChannel) {
    asio::io_context io;
    auto test = asio::co_spawn(io, ExerciseSession(), asio::use_future);
    io.run_for(std::chrono::seconds(3));
    ASSERT_EQ(test.wait_for(std::chrono::seconds(0)), std::future_status::ready);
    EXPECT_NO_THROW(test.get());
}
} // namespace
