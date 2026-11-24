#include "server.hpp"
#include "fetch_scheduler.hpp"
#include <boost/asio.hpp>
#include <deque>
#include <iostream>

namespace tb {
namespace {
namespace asio = boost::asio;
using asio::use_awaitable;
struct Session {
    asio::ip::tcp::socket socket;
    std::deque<std::string> queue;
    std::shared_ptr<FetchScheduler> scheduler;
    ReplyLimiter limiter;
    bool writing = false;
};

asio::awaitable<void> Drain(std::shared_ptr<Session> session) {
    while (!session->queue.empty()) {
        auto line = std::move(session->queue.front()) + "\r\n";
        session->queue.pop_front();
        co_await asio::async_write(session->socket, asio::buffer(line), use_awaitable);
    }
    session->writing = false;
}

void Send(std::shared_ptr<Session> session, std::string line, bool priority = false) {
    if (!session->socket.is_open()) {
        return;
    }
    if (session->queue.size() >= 64) {
        session->scheduler->RecordReplyDrop();
        if (!priority) {
            return;
        }
        session->queue.pop_back();
    }
    if (priority) {
        session->queue.push_front(std::move(line));
    } else {
        session->queue.push_back(std::move(line));
    }
    if (!session->writing) {
        session->writing = true;
        asio::co_spawn(session->socket.get_executor(), Drain(session),
                       [session](std::exception_ptr error) {
                           if (error) {
                               boost::system::error_code ignored;
                               session->socket.close(ignored);
                           }
                       });
    }
}

void Reply(const std::shared_ptr<Session>& session, const std::string& target, std::string_view title) {
    if (!session->socket.is_open()) {
        return;
    }
    auto prefix = "PRIVMSG " + target + " :";
    if (title.empty() || prefix.size() >= 510) {
        return;
    }
    auto limit = 510 - prefix.size();
    if (title.size() > limit) {
        while (limit && (static_cast<unsigned char>(title[limit]) & 192) == 128) {
            --limit;
        }
        title = title.substr(0, limit);
    }
    if (!session->limiter.Allow(target, Clock::now())) {
        session->scheduler->RecordReplyDrop();
        return;
    }
    auto line = std::move(prefix);
    for (char ch : title) {
        line += static_cast<unsigned char>(ch) < 32 || ch == 127 ? ' ' : ch;
    }
    Send(session, std::move(line));
}

asio::awaitable<bool> ServeSession(asio::ip::tcp::socket socket, std::string nick,
                                   std::string channels,
                                   std::shared_ptr<FetchScheduler> scheduler) {
    auto session = std::make_shared<Session>(Session{std::move(socket), {}, scheduler, {}});
    Send(session, "NICK " + nick);
    Send(session, "USER " + nick + " 0 * :titlebot");
    bool registered = false;
    std::string input;
    try {
        while (true) {
            auto count = co_await asio::async_read_until(
                session->socket, asio::dynamic_buffer(input, 8192), "\r\n", use_awaitable);
            auto line = input.substr(0, count - 2);
            input.erase(0, count);
            auto space = line.find(' ');
            if (line.starts_with("PING ")) {
                Send(session, "PONG " + line.substr(5), true);
            } else if (space != line.npos && line.compare(space, 5, " 001 ") == 0) {
                registered = true;
                Send(session, "JOIN " + channels);
            } else if (!line.empty()) {
                if (auto msg = parse_privmsg(line)) {
                    auto target = msg->channel == nick ? msg->who : msg->channel;
                    scheduler->Submit(std::move(*msg), [weak = std::weak_ptr(session),
                                                        target](const std::string& title) {
                        if (auto owner = weak.lock()) {
                            Reply(owner, target, title);
                        }
                    });
                }
            }
        }
    } catch (const boost::system::system_error&) {
        boost::system::error_code ignored;
        session->socket.close(ignored);
    }
    scheduler->Report();
    co_return registered;
}

} // namespace

asio::awaitable<bool> RunSession(asio::ip::tcp::socket socket, std::string nick,
                                 std::string channels, Fetch fetch) {
    auto scheduler = std::make_shared<FetchScheduler>(socket.get_executor(), std::move(fetch));
    return ServeSession(std::move(socket), std::move(nick), std::move(channels),
                        std::move(scheduler));
}

asio::awaitable<void> RunBot(std::string host, std::string port, std::string nick,
                             std::string channels, Fetch fetch) {
    auto executor = co_await asio::this_coro::executor;
    auto scheduler = std::make_shared<FetchScheduler>(executor, std::move(fetch));
    asio::ip::tcp::resolver resolver(executor);
    unsigned delay = 1;
    while (true) {
        try {
            auto endpoints = co_await resolver.async_resolve(host, port, asio::use_awaitable);
            asio::ip::tcp::socket socket(executor);
            co_await asio::async_connect(socket, endpoints, asio::use_awaitable);
            if (co_await ServeSession(std::move(socket), nick, channels, scheduler)) {
                delay = 1;
            }
        } catch (const boost::system::system_error&) {
            std::cerr << "IRC connection failed\n";
        }
        asio::steady_timer timer(executor, std::chrono::seconds(delay));
        co_await timer.async_wait(asio::use_awaitable);
        delay = std::min(60u, delay * 2);
    }
}

} // namespace tb
