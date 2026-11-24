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
    Clock::time_point burst_until{};
    unsigned burst = 0;
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

void Reply(const std::shared_ptr<Session>& session, const std::string& target,
           std::string_view title) {
    if (!session->socket.is_open()) {
        return;
    }
    auto prefix = "PRIVMSG " + target + " :";
    if (title.empty() || prefix.size() >= 430) {
        return;
    }
    auto limit = 430 - prefix.size();
    if (title.size() > limit) {
        while (limit && (static_cast<unsigned char>(title[limit]) & 192) == 128) {
            --limit;
        }
        title = title.substr(0, limit);
    }
    if (title.empty()) {
        return;
    }
    auto now = Clock::now();
    if (now >= session->burst_until) {
        session->burst_until = now + std::chrono::seconds(2);
        session->burst = 0;
    }
    if (session->burst >= 8 || !session->limiter.Allow(target, now)) {
        session->scheduler->RecordReplyDrop();
        return;
    }
    ++session->burst;
    auto line = std::move(prefix);
    for (char ch : title) {
        line += static_cast<unsigned char>(ch) < 32 || ch == 127 ? ' ' : ch;
    }
    Send(session, std::move(line));
}

bool SameNick(std::string_view a, std::string_view b) {
    auto fold = [](unsigned char c) {
        if (c >= 'A' && c <= ']') {
            return c + 32;
        }
        return c == '^' ? static_cast<int>('~') : static_cast<int>(c);
    };
    return std::ranges::equal(a, b, {}, fold, fold);
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
            std::string_view command(line);
            std::string_view source;
            if (command.starts_with(':')) {
                auto end = command.find(' ');
                if (end == command.npos) {
                    continue;
                }
                source = command.substr(1, end - 1);
                command.remove_prefix(end + 1);
            }
            auto space = command.find(' ');
            if (space == command.npos) {
                continue;
            }
            auto params = command.substr(space + 1);
            command = command.substr(0, space);
            if (command == "PING") {
                Send(session, "PONG " + std::string(params), true);
            } else if (command == "001") {
                registered = true;
                nick = params.substr(0, params.find(' '));
                Send(session, "JOIN " + channels);
            } else if (command == "433" && !registered) {
                session->socket.close();
                break;
            } else if (command == "NICK" && SameNick(source.substr(0, source.find('!')), nick)) {
                if (params.starts_with(':')) {
                    params.remove_prefix(1);
                }
                nick = params;
            } else if (command == "PRIVMSG") {
                if (auto msg = parse_privmsg(line)) {
                    if (SameNick(msg->who, nick)) {
                        continue;
                    }
                    auto target = SameNick(msg->channel, nick) ? msg->who : msg->channel;
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
