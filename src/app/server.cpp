#include "server.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <deque>
#include <exception>
#include <mutex>
#include <optional>
#include <random>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "common/deadline.hpp"
#include "common/log.hpp"
#include "fetch_scheduler.hpp"
#include "http/fetcher.hpp"
#include "irc/irc.hpp"

namespace tb {
namespace {
namespace asio = boost::asio;
using asio::use_awaitable;

// Use the traditional IRC nickname length for retries until ISUPPORT supplies NICKLEN.
constexpr std::size_t kDefaultNickLength = 9;

// Owns the socket and protocol state for one IRC connection. Fetch callbacks hold a weak
// reference to their originating session so a late title cannot be sent on a new connection.
struct Session {
    asio::ssl::stream<asio::ip::tcp::socket> transport;
    bool secure;
    std::string nick;
    std::optional<unsigned> nick_suffix;
    unsigned nick_retries = 0;
    std::string retry_nick;
    bool registered = false;
    Clock::time_point registered_at{};
    Clock::time_point last_input{};
    bool probe_sent = false;
    // Pending IRC lines without CRLF. A PONG can move ahead of queued title replies.
    std::deque<std::string> queue;
    // RunBot shares the fetch scheduler across reconnects.
    std::shared_ptr<FetchScheduler> scheduler;
    // The limiter counts title replies per target. The burst counter covers all targets
    // on this connection and resets when the two-second window expires at burst_until.
    ReplyLimits limits;
    ReplyLimiter limiter;
    Clock::time_point burst_until;
    unsigned burst = 0;
    // Only one Drain coroutine may write to the socket at a time.
    bool writing = false;
    std::shared_ptr<asio::steady_timer> quit_timer;
};

asio::awaitable<void> Drain(std::shared_ptr<Session> session) {
    try {
        while (!session->queue.empty()) {
            auto line = std::move(session->queue.front()) + "\r\n";
            session->queue.pop_front();
            if (session->secure) {
                co_await asio::async_write(session->transport, asio::buffer(line), use_awaitable);
            } else {
                co_await asio::async_write(session->transport.next_layer(), asio::buffer(line), use_awaitable);
            }
        }
        session->writing = false;
        if (session->quit_timer) {
            session->quit_timer->cancel();
        }
        co_return;
    } catch (const std::exception& ex) {
        TB_LOG_ERROR("IRC write failed: {}", ex.what());
    } catch (...) {
        TB_LOG_ERROR("IRC write failed with an unknown exception");
    }

    boost::system::error_code close_error;
    session->transport.next_layer().close(close_error);
    if (close_error) {
        TB_LOG_WARN("Failed to close IRC socket after write failure: {}", close_error.message());
    }
}

void Send(const std::shared_ptr<Session>& session, std::string line, bool priority = false) {
    if (!session->transport.next_layer().is_open()) {
        TB_LOG_WARN("Dropping outgoing IRC line: socket is closed");
        return;
    }

    if (session->queue.size() >= session->limits.queued) {
        session->scheduler->RecordReplyDrop();
        if (!priority || session->queue.empty()) {
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
        asio::co_spawn(session->transport.next_layer().get_executor(), Drain(session), asio::detached);
    }
}

void Reply(const std::shared_ptr<Session>& session, const std::string& target,
           std::string_view title) {
    // Limit the entire PRIVMSG line in bytes before Drain appends CRLF.
    const auto line_bytes = session->limits.line_bytes;

    if (session->quit_timer || !session->transport.next_layer().is_open()) {
        TB_LOG_DEBUG("Discarding title reply: IRC session is closed");
        return;
    }

    auto prefix = "PRIVMSG " + target + " :";
    if (title.empty()) {
        return;
    }
    if (prefix.size() >= line_bytes) {
        TB_LOG_WARN("Dropping title reply: {}-byte prefix exhausts the {}-byte IRC reply limit",
                    prefix.size(), line_bytes);
        return;
    }

    // If the first excluded byte is a UTF-8 continuation byte (10xxxxxx), the cut
    // splits a code point. Move the cut back to its leading byte so it is omitted in full.
    auto limit = line_bytes - prefix.size();
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
        session->burst_until = now + session->limits.window;
        session->burst = 0;
    }
    if (session->burst >= session->limits.per_session || !session->limiter.Allow(target, now)) {
        TB_LOG_WARN("Dropping IRC title reply: {} rate limit reached",
                    session->burst >= session->limits.per_session ? "session" : "per-target");
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

void UpdateNickLength(std::string_view params, std::size_t& limit) {
    TakeIrcToken(params); // Skip the recipient; tokens end at the trailing description.
    while (!params.empty() && !params.starts_with(':')) {
        auto token = TakeIrcToken(params);
        if (token == "-NICKLEN") {
            limit = kDefaultNickLength;
        } else if (token.starts_with("NICKLEN=")) {
            token.remove_prefix(std::string_view("NICKLEN=").size());
            std::size_t value = 0;
            auto end = token.data() + token.size();
            auto [ptr, ec] = std::from_chars(token.data(), end, value);
            if (ec == std::errc{} && ptr == end && value > 0) {
                limit = value;
            }
        }
    }
}

void RetryNickname(const std::shared_ptr<Session>& session, std::string_view rejected,
                   std::size_t& limit) {
    // Before ISUPPORT, a 433 may echo the server-truncated form of our last NICK.
    const auto& requested = session->retry_nick.empty() ? session->nick : session->retry_nick;
    if (!rejected.empty() && rejected.size() < requested.size() &&
        requested.starts_with(rejected)) {
        limit = std::min(limit, rejected.size());
    }
    // Keep one leading character. Stop before a short decimal suffix cycles.
    if (std::to_string(session->nick_retries).size() >= limit) {
        TB_LOG_WARN("No more numeric nickname retries fit NICKLEN={}", limit);
        return;
    }
    if (!session->nick_suffix) {
        session->nick_suffix = RandomNickSuffix();
    }
    auto suffix = std::to_string(*session->nick_suffix + session->nick_retries++);
    if (suffix.size() >= limit) {
        suffix.erase(0, suffix.size() - (limit - 1));
    }
    auto keep = std::min(session->nick.size(), limit - suffix.size());
    while (keep && keep < session->nick.size() &&
           (static_cast<unsigned char>(session->nick[keep]) & 0xc0) == 0x80) {
        --keep;
    }
    auto prefix = keep ? session->nick.substr(0, keep) : "_";
    session->retry_nick = prefix + suffix;
    Send(session, "NICK " + session->retry_nick);
}

bool SameNick(std::string_view a, std::string_view b) {
    return std::ranges::equal(a, b, {}, FoldIrcCase, FoldIrcCase);
}

asio::awaitable<void> CloseOnRegistrationTimeout(asio::awaitable<bool> expired,
                                                 std::shared_ptr<Session> session) {
    if (co_await std::move(expired)) {
        TB_LOG_WARN("IRC registration timed out before welcome; closing connection");
        boost::system::error_code ec;
        session->transport.next_layer().close(ec);
        if (ec) {
            TB_LOG_WARN("Failed to close IRC socket after registration timeout: {}", ec.message());
        }
    }
}

asio::awaitable<void> WatchIdle(std::shared_ptr<Session> session,
                                std::shared_ptr<asio::steady_timer> timer,
                                std::chrono::milliseconds idle, std::chrono::milliseconds grace) {
    while (session->transport.next_layer().is_open()) {
        const auto expiry = session->last_input + (session->probe_sent ? grace : idle);
        timer->expires_at(expiry);
        boost::system::error_code ec;
        co_await timer->async_wait(asio::redirect_error(use_awaitable, ec));
        if (ec || !session->transport.next_layer().is_open()) {
            co_return;
        }
        if (Clock::now() < session->last_input + (session->probe_sent ? grace : idle)) {
            continue;
        }
        if (session->probe_sent) {
            TB_LOG_WARN("IRC idle probe timed out");
            session->transport.next_layer().close(ec);
            co_return;
        }
        Send(session, "PING :titlebot", true);
        session->probe_sent = true;
        session->last_input = Clock::now();
    }
}

void HandlePrivMsg(const std::shared_ptr<Session>& session, std::string_view line) {
    auto msg = parse_privmsg(line);
    if (!msg || SameNick(msg->who, session->nick)) {
        return;
    }
    auto target = SameNick(msg->channel, session->nick) ? msg->who : msg->channel;
    FetchScheduler::Destination destination{session, target};
    session->scheduler->Submit(
        std::move(*msg),
        [weak = std::weak_ptr(session), target = std::move(target)](const std::string& title) {
            if (auto owner = weak.lock()) {
                Reply(owner, target, title);
            }
        }, std::move(destination));
}

std::string_view TrailingText(std::string_view params) {
    if (params.starts_with(':')) {
        params.remove_prefix(1);
    }
    return params;
}

void HandleLine(const std::shared_ptr<Session>& session, std::string_view line, // NOLINT
                const std::string& channels, Deadline& registration, std::size_t& nick_limit) {
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
    } else if (command == "005") {
        UpdateNickLength(params, nick_limit);
    } else if (command == "433" && !session->registered) {
        TakeIrcToken(params);
        RetryNickname(session, TakeIrcToken(params), nick_limit);
    } else if (command == "NICK" && SameNick(source.substr(0, source.find('!')), session->nick)) {
        session->nick = TrailingText(params);
    } else if (command == "ERROR") {
        TB_LOG_ERROR("IRC ERROR: {}", TrailingText(params));
    } else if (command == "KICK") {
        auto channel = TakeIrcToken(params);
        auto nick = TakeIrcToken(params);
        if (SameNick(nick, session->nick)) {
            TB_LOG_WARN("IRC KICK: {} removed us from {}: {}", source, channel,
                        TrailingText(params));
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

asio::awaitable<std::optional<bool>> ServeSession(asio::ip::tcp::socket socket, const BotConfig& config,
                                   std::shared_ptr<FetchScheduler> scheduler,
                                   std::size_t& nick_limit, asio::ssl::context& tls) {
    auto session = std::make_shared<Session>(Session{
        .transport = {std::move(socket), tls},
        .secure = config.tls,
        .nick = config.nick,
        .nick_suffix = {},
        .retry_nick = {},
        .queue = {},
        .scheduler = scheduler,
        .limits = config.replies,
        .limiter = {config.replies},
        .burst_until = {},
    });
    auto executor = session->transport.next_layer().get_executor();

    if (session->secure) {
        Deadline handshake(executor, Clock::now() + config.connect_timeout);
        asio::co_spawn(executor, CloseOnRegistrationTimeout(handshake.Wait(), session), asio::detached);
        session->transport.set_verify_mode(asio::ssl::verify_peer);
        session->transport.set_verify_callback(asio::ssl::host_name_verification(config.server));
        if (!SSL_set_tlsext_host_name(session->transport.native_handle(), config.server.c_str())) {
            throw boost::system::system_error(asio::error::invalid_argument);
        }
        co_await session->transport.async_handshake(asio::ssl::stream_base::client, use_awaitable);
    }
    Send(session, "NICK " + session->nick);
    Send(session, "USER " + session->nick + " 0 * :titlebot");

    Deadline registration(executor, Clock::now() + config.registration_timeout);
    asio::co_spawn(executor, CloseOnRegistrationTimeout(registration.Wait(), session),
                   asio::detached);

    auto idle_timer = std::make_shared<asio::steady_timer>(executor);
    std::string input;
    try {
        while (true) {
            auto read = [&](auto& stream) {
                return asio::async_read_until(stream, asio::dynamic_buffer(input, 8192), "\r\n", use_awaitable);
            };
            std::size_t count;
            if (session->secure) {
                count = co_await read(session->transport);
            } else {
                count = co_await read(session->transport.next_layer());
            }
            auto line = std::string_view(input).substr(0, count - 2);
            const bool was_registered = session->registered;
            session->last_input = Clock::now();
            session->probe_sent = false;
            HandleLine(session, line, config.channels, registration, nick_limit);
            input.erase(0, count);
            if (!was_registered && session->registered) {
                session->registered_at = session->last_input;
                asio::co_spawn(
                    executor,
                    WatchIdle(session, idle_timer, config.idle_timeout, config.ping_timeout),
                    asio::detached);
            }
        }
    } catch (const boost::system::system_error& ex) {
        if (ex.code() != asio::error::operation_aborted && session->transport.next_layer().is_open()) {
            TB_LOG_WARN("IRC read failed: {}", ex.what());
        }
    }
    idle_timer->cancel();
    registration.Cancel();
    const auto cancellation = co_await asio::this_coro::cancellation_state;
    if (cancellation.cancelled() != asio::cancellation_type::none &&
        session->transport.next_layer().is_open()) {
        co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation());
        session->quit_timer = std::make_shared<asio::steady_timer>(executor, std::chrono::seconds(1));
        session->queue.clear();
        Send(session, "QUIT :Shutting down", true);
        boost::system::error_code ignored;
        co_await session->quit_timer->async_wait(asio::redirect_error(use_awaitable, ignored));
        session->transport.next_layer().close(ignored);
        scheduler->Report();
        co_return std::nullopt;
    }
    boost::system::error_code ignored;
    session->transport.next_layer().close(ignored);
    scheduler->Report();
    const auto duration = Clock::now() - session->registered_at;
    co_return session->registered && duration >= config.stable_session;
}

asio::awaitable<asio::ip::tcp::socket> Connect(const BotConfig& config) {
    auto executor = co_await asio::this_coro::executor;
    auto resolver = std::make_shared<asio::ip::tcp::resolver>(executor);
    auto socket = std::make_shared<asio::ip::tcp::socket>(executor);
    const auto expires = Clock::now() + config.connect_timeout;
    Deadline deadline(executor, expires);
    asio::co_spawn(
        executor,
        [resolver, socket, wait = deadline.Wait()]() mutable -> asio::awaitable<void> {
            if (co_await std::move(wait)) {
                resolver->cancel();
                boost::system::error_code ignored;
                socket->close(ignored);
            }
        },
        asio::detached);
    auto endpoints = co_await resolver->async_resolve(config.server, config.port, use_awaitable);
    co_await asio::async_connect(
        *socket, endpoints,
        [expires](boost::system::error_code, const asio::ip::tcp::endpoint&) {
            return Clock::now() < expires;
        },
        use_awaitable);
    co_return std::move(*socket);
}

} // namespace

asio::awaitable<void> RunBot(BotConfig config) {
    auto tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_client);
    tls->set_default_verify_paths();
    auto executor = co_await asio::this_coro::executor;
    auto scheduler = std::make_shared<FetchScheduler>(
        executor, [tls, limits = config.fetch](PrivMsg msg) { return FetchTitle(std::move(msg), *tls, limits); }, config.scheduler);
    scheduler->EnableReports();
    std::size_t nick_limit = kDefaultNickLength;
    const auto cancellation = co_await asio::this_coro::cancellation_state;
    unsigned delay = 1;
    while (true) {
        try {
            auto socket = co_await Connect(config);
            auto stable = co_await ServeSession(std::move(socket), config, scheduler, nick_limit, *tls);
            if (!stable) {
                co_return;
            }
            if (*stable) {
                delay = 1;
            }
        } catch (const boost::system::system_error& ex) {
            if (ex.code() == asio::error::operation_aborted && cancellation.cancelled() != asio::cancellation_type::none) {
                co_return;
            }
            TB_LOG_WARN("IRC connection failed: {}. Retrying in {}s", ex.what(), delay);
        }
        asio::steady_timer timer(executor, std::chrono::seconds(delay));
        co_await timer.async_wait(asio::use_awaitable);
        delay = std::min(60u, delay * 2);
    }
}

} // namespace tb
