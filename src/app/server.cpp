#include "server.hpp"
#include <boost/asio.hpp>
#include <deque>

namespace tb {
namespace {
namespace asio = boost::asio;
using asio::use_awaitable;
struct Session {
    asio::ip::tcp::socket socket;
    std::deque<std::string> queue;
    unsigned active = 0;
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

asio::awaitable<void> Reply(std::shared_ptr<Session> session, Fetch fetch, PrivMsg msg,
                            std::string target) {
    auto result = co_await fetch(std::move(msg));
    if (!result || !*result) {
        co_return;
    }
    auto title = std::move(**result);
    for (char& ch : title) {
        if (static_cast<unsigned char>(ch) < 32 || ch == 127) {
            ch = ' ';
        }
    }
    auto prefix = "PRIVMSG " + target + " :";
    if (title.empty() || prefix.size() >= 510) {
        co_return;
    }
    auto limit = 510 - prefix.size();
    if (title.size() > limit) {
        while (limit && (static_cast<unsigned char>(title[limit]) & 192) == 128) {
            --limit;
        }
        title.resize(limit);
    }
    Send(session, prefix + title);
}
} // namespace

asio::awaitable<void> RunSession(asio::ip::tcp::socket socket, std::string nick,
                                 std::string channels, Fetch fetch) {
    auto session = std::make_shared<Session>(Session{std::move(socket), {}});
    Send(session, "NICK " + nick);
    Send(session, "USER " + nick + " 0 * :titlebot");
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
                Send(session, "JOIN " + channels);
            } else if (!line.empty() && session->active < 8) {
                if (auto msg = parse_privmsg(line)) {
                    auto target = msg->channel == nick ? msg->who : msg->channel;
                    ++session->active;
                    asio::co_spawn(session->socket.get_executor(),
                                   Reply(session, fetch, std::move(*msg), target),
                                   [session](std::exception_ptr) { --session->active; });
                }
            }
        }
    } catch (const boost::system::system_error&) {
        boost::system::error_code ignored;
        session->socket.close(ignored);
    }
}
} // namespace tb
