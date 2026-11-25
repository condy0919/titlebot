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
#include "irc/policy.hpp"

namespace tb {
namespace {
namespace asio = boost::asio;
using asio::use_awaitable;

// Owns the socket and protocol state for one IRC connection. Fetch callbacks hold a weak
// reference to their originating session so a late title cannot be sent on a new connection.
struct Session {
    asio::ssl::stream<asio::ip::tcp::socket> transport;
    bool secure;
    std::string nick;
    NicknameNegotiator& nickname;
    bool registered = false;
    Clock::time_point registered_at{};
    Clock::time_point last_input{};
    bool probe_sent = false;
    // Pending IRC lines without CRLF. A PONG can move ahead of queued title replies.
    std::deque<std::string> queue;
    // RunBot shares the fetch scheduler across reconnects.
    std::shared_ptr<FetchScheduler> scheduler;
    ReplyLimits limits;
    IrcReplyPolicy limiter;
    // Only one Drain coroutine may write to the socket at a time.
    bool writing = false;
    std::shared_ptr<asio::steady_timer> quit_timer{};
};

asio::awaitable<void> Drain(std::shared_ptr<Session> session) {
    try {
        while (!session->queue.empty()) {
            auto line = std::move(session->queue.front()) + "\r\n";
            session->queue.pop_front();
            if (session->secure) {
                co_await asio::async_write(session->transport, asio::buffer(line), use_awaitable);
            } else {
                co_await asio::async_write(session->transport.next_layer(), asio::buffer(line),
                                           use_awaitable);
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
        asio::co_spawn(session->transport.next_layer().get_executor(), Drain(session),
                       asio::detached);
    }
}

void Reply(const std::shared_ptr<Session>& session, const std::string& target,
           std::string_view title) {
    if (session->quit_timer || !session->transport.next_layer().is_open()) {
        TB_LOG_DEBUG("Discarding title reply: IRC session is closed");
        return;
    }
    auto line = FormatReplyLine(target, title, session->limits.line_bytes);
    if (!line || !session->limiter.Allow(target, Clock::now())) {
        TB_LOG_WARN("Dropping unformattable or rate-limited IRC reply");
        session->scheduler->RecordReplyDrop();
        return;
    }
    Send(session, std::move(*line));
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
        },
        std::move(destination));
}

std::string_view TrailingText(std::string_view params) {
    if (params.starts_with(':')) {
        params.remove_prefix(1);
    }
    return params;
}

void HandleLine(const std::shared_ptr<Session>& session, std::string_view line, // NOLINT
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
    } else if (command == "005") {
        session->nickname.Observe(params);
    } else if (command == "433" && !session->registered) {
        TakeIrcToken(params);
        if (auto retry = session->nickname.Retry(session->nick, TakeIrcToken(params))) {
            Send(session, "NICK " + *retry);
        } else {
            TB_LOG_WARN("Nickname retries exhausted");
        }
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

asio::awaitable<std::optional<bool>> ServeSession(asio::ip::tcp::socket socket,
                                                  const BotConfig& config,
                                                  std::shared_ptr<FetchScheduler> scheduler,
                                                  NicknameNegotiator& nickname,
                                                  asio::ssl::context& tls) {
    auto session = std::make_shared<Session>(Session{
        .transport = {std::move(socket), tls},
        .secure = config.tls,
        .nick = config.nick,
        .nickname = nickname,
        .queue = {},
        .scheduler = scheduler,
        .limits = config.replies,
        .limiter = {config.replies},
    });
    auto executor = session->transport.next_layer().get_executor();

    if (session->secure) {
        Deadline handshake(executor, Clock::now() + config.connect_timeout);
        asio::co_spawn(executor, CloseOnRegistrationTimeout(handshake.Wait(), session),
                       asio::detached);
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
                return asio::async_read_until(stream, asio::dynamic_buffer(input, 8192), "\r\n",
                                              use_awaitable);
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
            HandleLine(session, line, config.channels, registration);
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
        if (ex.code() != asio::error::operation_aborted &&
            session->transport.next_layer().is_open()) {
            TB_LOG_WARN("IRC read failed: {}", ex.what());
        }
    }
    idle_timer->cancel();
    registration.Cancel();
    const auto cancellation = co_await asio::this_coro::cancellation_state;
    if (cancellation.cancelled() != asio::cancellation_type::none &&
        session->transport.next_layer().is_open()) {
        co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation());
        session->quit_timer =
            std::make_shared<asio::steady_timer>(executor, std::chrono::seconds(1));
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
    co_return session->registered&& duration >= config.stable_session;
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
        executor,
        [tls, limits = config.fetch](PrivMsg msg) {
            return FetchTitle(std::move(msg), *tls, limits);
        },
        config.scheduler);
    scheduler->EnableReports();
    NicknameNegotiator nickname;
    const auto cancellation = co_await asio::this_coro::cancellation_state;
    unsigned delay = 1;
    while (true) {
        try {
            auto socket = co_await Connect(config);
            nickname.Begin();
            auto stable =
                co_await ServeSession(std::move(socket), config, scheduler, nickname, *tls);
            if (!stable) {
                co_return;
            }
            if (*stable) {
                delay = 1;
            }
        } catch (const boost::system::system_error& ex) {
            if (ex.code() == asio::error::operation_aborted &&
                cancellation.cancelled() != asio::cancellation_type::none) {
                scheduler->Report();
                co_return;
            }
            TB_LOG_WARN("IRC connection failed: {}. Retrying in {}s", ex.what(), delay);
        }
        asio::steady_timer timer(executor, std::chrono::seconds(delay));
        boost::system::error_code ec;
        co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, ec));
        if (ec == asio::error::operation_aborted &&
            cancellation.cancelled() != asio::cancellation_type::none) {
            scheduler->Report();
            co_return;
        }
        if (ec) {
            throw boost::system::system_error(ec);
        }
        delay = std::min(60u, delay * 2);
    }
}

} // namespace tb
