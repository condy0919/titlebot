#include "server.hpp"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <exception>
#include <mutex>
#include <optional>
#include <random>

#include <boost/asio.hpp>
#include <boost/asio/ssl/context.hpp>

#include "common/deadline.hpp"
#include "common/log.hpp"
#include "fetch_scheduler.hpp"
#include "http/fetcher.hpp"
#include "irc/irc.hpp"

namespace tb {
namespace {
namespace asio = boost::asio;
using asio::use_awaitable;

// Owns the socket and protocol state for one IRC connection. Fetch callbacks hold a weak
// reference to their originating session so a late title cannot be sent on a new connection.
struct Session {
    asio::ip::tcp::socket socket;
    std::string nick;
    std::optional<unsigned> nick_suffix;
    bool registered = false;
    // Pending IRC lines without CRLF. A PONG can move ahead of queued title replies.
    std::deque<std::string> queue;
    // RunBot shares the fetch scheduler across reconnects.
    std::shared_ptr<FetchScheduler> scheduler;
    // The limiter counts title replies per target. The burst counter covers all targets
    // on this connection and resets when the two-second window expires at burst_until.
    ReplyLimiter limiter;
    Clock::time_point burst_until;
    unsigned burst = 0;
    // Only one Drain coroutine may write to the socket at a time.
    bool writing = false;
};

asio::awaitable<void> Drain(std::shared_ptr<Session> session) {
    try {
        while (!session->queue.empty()) {
            auto line = std::move(session->queue.front()) + "\r\n";
            session->queue.pop_front();
            co_await asio::async_write(session->socket, asio::buffer(line), use_awaitable);
        }
        session->writing = false;
        co_return;
    } catch (const std::exception& ex) {
        TB_LOG_ERROR("IRC write failed: {}", ex.what());
    } catch (...) {
        TB_LOG_ERROR("IRC write failed with an unknown exception");
    }

    boost::system::error_code close_error;
    session->socket.close(close_error);
    if (close_error) {
        TB_LOG_WARN("Failed to close IRC socket after write failure: {}", close_error.message());
    }
}

void Send(const std::shared_ptr<Session>& session, std::string line, bool priority = false) {
    if (!session->socket.is_open()) {
        TB_LOG_WARN("Dropping outgoing IRC line: socket is closed");
        return;
    }

    if (session->queue.size() >= 64) {
        session->scheduler->RecordReplyDrop();
        if (!priority) {
            TB_LOG_WARN("Dropping new outgoing IRC line: IRC send queue is full ({} lines)",
                        session->queue.size());
            return;
        }
        TB_LOG_WARN("Evicting last queued IRC line for priority send: queue is full ({} lines)",
                    session->queue.size());
        session->queue.pop_back();
    }

    if (priority) {
        session->queue.push_front(std::move(line));
    } else {
        session->queue.push_back(std::move(line));
    }

    if (!session->writing) {
        session->writing = true;
        asio::co_spawn(session->socket.get_executor(), Drain(session), asio::detached);
    }
}

void Reply(const std::shared_ptr<Session>& session, const std::string& target,
           std::string_view title) {
    // Limit the entire PRIVMSG line in bytes before Drain appends CRLF.
    constexpr std::size_t kMaxReplyLineBytes = 430;

    if (!session->socket.is_open()) {
        TB_LOG_DEBUG("Discarding title reply: IRC session is closed");
        return;
    }

    auto prefix = "PRIVMSG " + target + " :";
    if (title.empty()) {
        return;
    }
    if (prefix.size() >= kMaxReplyLineBytes) {
        TB_LOG_WARN("Dropping title reply: {}-byte prefix exhausts the {}-byte IRC reply limit",
                    prefix.size(), kMaxReplyLineBytes);
        return;
    }

    // If the first excluded byte is a UTF-8 continuation byte (10xxxxxx), the cut
    // splits a code point. Move the cut back to its leading byte so it is omitted in full.
    auto limit = kMaxReplyLineBytes - prefix.size();
    if (title.size() > limit) {
        while (limit && (static_cast<unsigned char>(title[limit]) & 192) == 128) {
            --limit;
        }
        title = title.substr(0, limit);
    }

    if (title.empty()) {
        TB_LOG_WARN("Dropping title reply: first code point exceeds the remaining byte budget");
        return;
    }

    auto now = Clock::now();
    if (now >= session->burst_until) {
        session->burst_until = now + std::chrono::seconds(2);
        session->burst = 0;
    }
    if (session->burst >= 8 || !session->limiter.Allow(target, now)) {
        TB_LOG_WARN("Dropping IRC title reply: {} rate limit reached",
                    session->burst >= 8 ? "session" : "per-target");
        session->scheduler->RecordReplyDrop();
        return;
    }
    ++session->burst;

    auto line = std::move(prefix);
    // Replace ASCII control bytes (0x00-0x1F and DEL) with spaces so a title
    // cannot insert a line break or change IRC text formatting.
    for (char ch : title) {
        line += static_cast<unsigned char>(ch) < 32 || ch == 127 ? ' ' : ch;
    }
    Send(session, std::move(line));
}

unsigned RandomNickSuffix() {
    static std::mt19937 generator{std::random_device{}()};
    static std::mutex mutex;
    std::scoped_lock lock(mutex);
    return std::uniform_int_distribution<unsigned>(0, 65535)(generator);
}

bool SameNick(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, {}, FoldIrcCase, FoldIrcCase);
}

asio::awaitable<void> CloseOnRegistrationTimeout(asio::awaitable<bool> expired,
                                                 std::shared_ptr<Session> session) {
    if (co_await std::move(expired)) {
        TB_LOG_WARN("IRC registration timed out before welcome; closing connection");
        boost::system::error_code ec;
        session->socket.close(ec);
        if (ec) {
            TB_LOG_WARN("Failed to close IRC socket after registration timeout: {}", ec.message());
        }
    }
}

void HandlePrivMsg(const std::shared_ptr<Session>& session, const std::string& line) {
    auto msg = parse_privmsg(line);
    if (!msg || SameNick(msg->who, session->nick)) {
        return;
    }
    auto target = SameNick(msg->channel, session->nick) ? msg->who : msg->channel;
    session->scheduler->Submit(
        std::move(*msg),
        [weak = std::weak_ptr(session), target = std::move(target)](const std::string& title) {
            if (auto owner = weak.lock()) {
                Reply(owner, target, title);
            }
        });
}

std::string_view TrailingText(std::string_view params) {
    if (params.starts_with(':')) {
        params.remove_prefix(1);
    }
    return params;
}

void HandleLine(const std::shared_ptr<Session>& session, const std::string& line, // NOLINT
                const std::string& channels, Deadline& registration) {
    std::string_view params(line);
    std::string_view source;
    if (params.starts_with(':')) {
        source = TakeIrcToken(params).substr(1);
    }
    auto command = TakeIrcToken(params);
    if (params.empty()) {
        return;
    }

    if (command == "PING") {
        Send(session, "PONG " + std::string(params), /*priority=*/true);
    } else if (command == "001") {
        registration.Cancel();
        session->registered = true;
        session->nick = TakeIrcToken(params);
        Send(session, "JOIN " + channels);
    } else if (command == "433" && !session->registered) {
        if (session->nick_suffix) {
            ++*session->nick_suffix;
        } else {
            session->nick_suffix = RandomNickSuffix();
        }
        Send(session, "NICK " + session->nick + std::to_string(*session->nick_suffix));
    } else if (command == "NICK" && SameNick(source.substr(0, source.find('!')), session->nick)) {
        session->nick = TrailingText(params);
    } else if (command == "ERROR") {
        TB_LOG_ERROR("IRC ERROR: {}", TrailingText(params));
    } else if (command == "KICK") {
        auto channel = TakeIrcToken(params);
        auto nick = TakeIrcToken(params);
        if (SameNick(nick, session->nick)) {
            TB_LOG_WARN("IRC KICK: {} removed us from {}: {}", source, channel, TrailingText(params));
        } else {
            TB_LOG_INFO("IRC KICK: {} removed {} from {}: {}", source, nick, channel,
                        TrailingText(params));
        }
    } else if (command == "437" || command == "471" || command == "473" || command == "474" ||
               command == "475") {
        TakeIrcToken(params); // Skip the numeric reply's recipient.
        auto target = TakeIrcToken(params);
        if (command == "437") {
            TB_LOG_WARN("IRC 437: {} temporarily unavailable: {}", target, TrailingText(params));
        } else {
            TB_LOG_WARN("IRC JOIN {} failed ({}): {}", target, command, TrailingText(params));
        }
    } else if (command == "PRIVMSG") {
        HandlePrivMsg(session, line);
    }
}

asio::awaitable<bool> ServeSession(asio::ip::tcp::socket socket, std::string nick, // NOLINT
                                   std::string channels, std::shared_ptr<FetchScheduler> scheduler,
                                   std::chrono::milliseconds registration_timeout) {
    auto session = std::make_shared<Session>(Session{
        .socket = std::move(socket),
        .nick = std::move(nick),
        .nick_suffix = {},
        .queue = {},
        .scheduler = scheduler,
        .limiter = {},
        .burst_until = {},
    });
    auto executor = session->socket.get_executor();

    Send(session, "NICK " + session->nick);
    Send(session, "USER " + session->nick + " 0 * :titlebot");

    Deadline registration(executor, Clock::now() + registration_timeout);
    asio::co_spawn(executor, CloseOnRegistrationTimeout(registration.Wait(), session),
                   asio::detached);

    std::string input;
    try {
        while (true) {
            auto count = co_await asio::async_read_until(
                session->socket, asio::dynamic_buffer(input, 8192), "\r\n", use_awaitable);
            auto line = input.substr(0, count - 2);
            input.erase(0, count);
            HandleLine(session, line, channels, registration);
        }
    } catch (const boost::system::system_error& ex) {
        if (session->socket.is_open()) {
            TB_LOG_WARN("IRC read failed: {}", ex.what());
        }
        boost::system::error_code ec;
        session->socket.close(ec);
        if (ec) {
            TB_LOG_WARN("Failed to close IRC socket after read failure: {}", ec.message());
        }
    }
    scheduler->Report();
    co_return session->registered;
}

} // namespace

asio::awaitable<void> RunBot(BotConfig config) {
    auto tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_client);
    tls->set_default_verify_paths();
    auto executor = co_await asio::this_coro::executor;
    auto scheduler = std::make_shared<FetchScheduler>(
        executor, [tls](PrivMsg msg) { return FetchTitle(std::move(msg), *tls); });
    asio::ip::tcp::resolver resolver(executor);
    unsigned delay = 1;
    while (true) {
        try {
            auto endpoints =
                co_await resolver.async_resolve(config.server, config.port, asio::use_awaitable);
            asio::ip::tcp::socket socket(executor);
            co_await asio::async_connect(socket, endpoints, asio::use_awaitable);
            if (co_await ServeSession(std::move(socket), config.nick, config.channels, scheduler,
                                      config.registration_timeout)) {
                delay = 1;
            }
        } catch (const boost::system::system_error& ex) {
            TB_LOG_WARN("IRC connection failed: {}. Retrying in {}s", ex.what(), delay);
        }
        asio::steady_timer timer(executor, std::chrono::seconds(delay));
        co_await timer.async_wait(asio::use_awaitable);
        delay = std::min(60u, delay * 2);
    }
}

} // namespace tb
