#include "ircbot/client.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <deque>
#include <exception>
#include <optional>
#include <random>
#include <stdexcept>
#include <utility>

#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/ssl.hpp>

#include "common/any_stream.hpp"
#include "common/connect.hpp"
#include "common/deadline.hpp"
#include "session_timer.hpp"

namespace ircbot {
namespace asio = boost::asio;
using asio::use_awaitable;
using Clock = std::chrono::steady_clock;
std::expected<void, ClockError> ManualClock::Advance(std::chrono::steady_clock::duration elapsed) {
    if (elapsed < elapsed.zero() || elapsed > TimePoint::max() - now_) {
        return std::unexpected(ClockError::kInvalidAdvance);
    }
    now_ += elapsed;
    for (auto& weak : alarms_) {
        if (auto alarm = weak.lock()) {
            alarm->Advance(now_);
        }
    }
    return {};
}

namespace detail {
struct QueuedLine {
    std::string text;
    bool paced;
};

struct DesiredChannel {
    std::string name;
    std::string key;
};
using Channels = std::vector<DesiredChannel>;

struct Session {
    std::unique_ptr<Transport> transport;
    asio::any_io_executor executor;
    Handlers handlers;
    Timeouts timeouts;
    std::shared_ptr<ManualClock> clock;
    std::size_t queue_limit;
    ServerFeatures features;
    std::shared_ptr<Channels> channels;
    Registration registration;
    bool require_auth;
    std::string nick;
    std::deque<QueuedLine> queue;
    std::chrono::milliseconds send_interval;
    std::function<std::chrono::milliseconds(std::string_view)> send_delay;
    std::optional<SessionTimer> pacing;
    Clock::time_point next_send{};
    std::optional<SessionTimer> write_timer;
    std::size_t write_generation = 0;
    std::optional<asio::steady_timer> writable;
    bool writing = false;
    bool closing = false;
    std::stop_source stop;
    std::shared_ptr<void> identity = std::make_shared<char>();
    bool registered = false;
    bool probe_sent = false;
    Clock::time_point last_input{};
    Clock::time_point registered_at{};
    std::shared_ptr<SessionTimer> quit_timer;
    std::optional<asio::experimental::channel<void(boost::system::error_code, bool)>> writer_done;
};
} // namespace detail
namespace {
using tb::Deadline;
using detail::Session;
using detail::SessionTimer;
using SessionDeadline = tb::BasicDeadline<SessionTimer>;

void Diagnose(const Handlers& handlers, std::string_view text,
              DiagnosticKind kind = DiagnosticKind::kProtocol, Severity level = Severity::kWarning,
              boost::system::error_code code = {}, std::optional<RegistrationError> registration = {},
              std::exception_ptr exception = {}, std::optional<long> certificate_error = {}) {
    if (handlers.diagnostic_event) {
        try {
            handlers.diagnostic_event({level, kind, text, code, registration, exception, certificate_error});
        } catch (...) {
        }
    }
    if (handlers.diagnostic) {
        // Diagnostics must never throw out of a socket/timer completion.
        try {
            handlers.diagnostic(text);
        } catch (...) {
        }
    }
}

void Diagnose(const std::shared_ptr<Session>& session, std::string_view text) {
    if (!session->closing) {
        Diagnose(session->handlers, text);
    }
}

void Close(const std::shared_ptr<Session>& session) {
    session->transport->Close();
    if (session->writable) {
        session->writable->cancel();
    }
    if (session->pacing) {
        session->pacing->cancel();
    }
}

asio::awaitable<void> CloseOnTimeout(asio::awaitable<bool> expired,
                                     std::shared_ptr<Session> session, std::string_view reason) {
    if (co_await std::move(expired)) {
        Diagnose(session, reason);
        Close(session);
    }
}

asio::awaitable<void> Drain(std::shared_ptr<Session> session) {
    try {
        while (session->transport->IsOpen() && !session->queue.empty()) {
            if (session->queue.front().paced && (session->send_interval.count() > 0 || session->send_delay)) {
                const auto now = detail::Now(session->clock);
                if (now < session->next_send) {
                    session->pacing.emplace(session->executor, session->clock, session->next_send);
                    boost::system::error_code ec;
                    co_await session->pacing->async_wait(asio::redirect_error(use_awaitable, ec));
                    session->pacing.reset();
                    continue;
                }
                auto interval = session->send_interval;
                if (session->send_delay) {
                    interval = std::clamp(session->send_delay(session->queue.front().text),
                                          std::chrono::milliseconds::zero(),
                                          std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::hours(24)));
                }
                session->next_send = now + interval;
            }
            auto line = std::move(session->queue.front().text);
            line += "\r\n";
            session->queue.pop_front();
            if (session->writable) {
                session->writable->cancel();
            }
            if (!session->write_timer) {
                session->write_timer.emplace(session->executor, session->clock);
            }
            const auto generation = ++session->write_generation;
            session->write_timer->expires_after(session->timeouts.write_timeout);
            session->write_timer->async_wait([session, generation](boost::system::error_code ec) {
                if (!ec && session->write_generation == generation) {
                    Diagnose(session, "IRC write timed out");
                    Close(session);
                }
            });
            auto written = co_await session->transport->Write(asio::buffer(line));
            if (!written) { throw boost::system::system_error(written.error()); }
            ++session->write_generation;
            session->write_timer->cancel();
        }
    } catch (const std::exception&) {
        ++session->write_generation;
        if (session->write_timer) {
            session->write_timer->cancel();
        }
        Diagnose(session, "IRC write failed");
        Close(session);
    }
    session->writing = false;
    if (session->writer_done) {
        session->writer_done->try_send(boost::system::error_code{}, true);
    }
    if (session->quit_timer) {
        session->quit_timer->cancel();
    }
}

bool PaceCommand(std::string_view command) {
    return !Equals(command, "PONG", CaseMapping::kAscii) &&
           !Equals(command, "CAP", CaseMapping::kAscii) &&
           !Equals(command, "AUTHENTICATE", CaseMapping::kAscii) &&
           !Equals(command, "QUIT", CaseMapping::kAscii);
}

std::expected<void, SendError> Queue(const std::shared_ptr<Session>& session, std::string line,
                                     bool priority = false, bool paced = true) {
    if (!session->transport->IsOpen()) {
        return std::unexpected(SendError::kClosed);
    }
    if (session->queue.size() >= session->queue_limit) {
        if ((!priority && paced) || !session->queue.back().paced) {
            return std::unexpected(SendError::kQueueFull);
        }
        session->queue.pop_back();
        Diagnose(session, "Evicting outgoing IRC line for priority send");
    }
    if (priority) {
        session->queue.push_front({std::move(line), false});
    } else if (!paced) {
        const auto first_business = std::ranges::find(session->queue, true, &detail::QueuedLine::paced);
        session->queue.insert(first_business, {std::move(line), false});
    } else {
        session->queue.push_back({std::move(line), true});
    }
    if ((priority || !paced) && session->pacing) {
        session->pacing->cancel();
    }
    if (!session->writing) {
        session->writing = true;
        asio::co_spawn(session->executor, Drain(session), asio::detached);
    }
    return {};
}

// Registration lines are ordered and must not be silently lost to application backpressure.
void SendControl(const std::shared_ptr<Session>& session, std::string line) {
    const bool paced = line.starts_with("JOIN ");
    if (!Queue(session, std::move(line), false, paced)) {
        Diagnose(session, "IRC control queue is full; closing connection");
        Close(session);
    }
}

asio::awaitable<void> WatchIdle(std::shared_ptr<Session> session,
                                std::shared_ptr<SessionTimer> timer) {
    while (session->transport->IsOpen()) {
        auto interval =
            session->probe_sent ? session->timeouts.ping_timeout : session->timeouts.idle_timeout;
        timer->expires_at(session->last_input + interval);
        boost::system::error_code ec;
        co_await timer->async_wait(asio::redirect_error(use_awaitable, ec));
        if (ec || !session->transport->IsOpen()) {
            co_return;
        }
        interval =
            session->probe_sent ? session->timeouts.ping_timeout : session->timeouts.idle_timeout;
        if (detail::Now(session->clock) < session->last_input + interval) {
            continue;
        }
        if (session->probe_sent) {
            Diagnose(session, "IRC idle probe timed out");
            Close(session);
            co_return;
        }
        if (!Queue(session, "PING :ircbot", true)) {
            co_return;
        }
        session->probe_sent = true;
        session->last_input = detail::Now(session->clock);
    }
}

struct Nicknames {
    std::size_t limit = 9;
    std::optional<unsigned> suffix;
    unsigned retries = 0;
    std::string last;

    void Begin() {
        suffix.reset();
        retries = 0;
        last.clear();
    }
    std::optional<std::string> Retry(const std::string& nick, std::string_view rejected) {
        const auto& requested = last.empty() ? nick : last;
        if (!rejected.empty() && rejected.size() < requested.size() &&
            requested.starts_with(rejected)) {
            limit = std::min(limit, rejected.size());
        }
        if (std::to_string(retries).size() >= limit) {
            return {};
        }
        if (!suffix) {
            suffix = std::random_device{}() % 65536;
        }
        auto digits = std::to_string(*suffix + retries++);
        if (digits.size() >= limit) {
            digits.erase(0, digits.size() - (limit - 1));
        }
        auto keep = std::min(nick.size(), limit - digits.size());
        while (keep && keep < nick.size() &&
               (static_cast<unsigned char>(nick[keep]) & 0xc0) == 0x80) {
            --keep;
        }
        last = (keep ? nick.substr(0, keep) : "_") + digits;
        return last;
    }
};

struct Fatal {
    ClientError error;
    std::exception_ptr exception{};
};

template <class Callback, class... Args>
void Invoke(const Callback& callback, Args&&... args) {
    if (callback) {
        try {
            callback(std::forward<Args>(args)...);
        } catch (...) {
            throw Fatal{ClientError::kHandler, std::current_exception()};
        }
    }
}

void Handle(const std::shared_ptr<Session>& session, const MessageView& message,
            const Config& config, Nicknames& nicknames, SessionDeadline& registration) {
    if (IsCommand(message, "PING") && !message.params.empty()) {
        std::string pong = "PONG";
        for (std::size_t i = 0; i < message.params.size(); ++i) {
            pong += i + 1 == message.params.size() ? " :" : " ";
            pong += message.params[i];
        }
        if (pong.size() > 510) {
            auto compact = SerializeMessage("PONG", {message.params.data(), message.params.size()});
            if (compact) {
                pong = std::move(*compact);
            }
        }
        if (pong.size() <= 510 && !Queue(session, std::move(pong), true)) {
            Close(session);
        }
    }
    auto actions = session->registration.Handle(message);
    if (!actions) {
        Diagnose(session->handlers, "IRC capability negotiation or SASL authentication failed",
                 DiagnosticKind::kRegistration, Severity::kError, {}, actions.error());
        throw Fatal{ClientError::kAuthentication};
    }
    for (auto& line : *actions) {
        SendControl(session, std::move(line));
    }
    if (IsCommand(message, "CAP") && message.params.size() >= 2 &&
        (Equals(message.params[1], "NEW", CaseMapping::kAscii) ||
         Equals(message.params[1], "DEL", CaseMapping::kAscii))) {
        Invoke(session->handlers.capabilities_changed, Connection(session), message);
    }
    if (IsCommand(message, "005")) {
        session->features.Observe(message);
        nicknames.limit = session->features.nick_length;
        Invoke(session->handlers.isupport_changed, Connection(session), message);
    } else if (!session->registered && IsCommand(message, "432") && message.params.size() >= 2) {
        Diagnose(session, "IRC server rejected nickname: " + std::string(message.params[1]));
        throw Fatal{ClientError::kInvalidNickname};
    } else if (!session->registered && message.params.size() >= 2 &&
               (IsCommand(message, "433") || IsCommand(message, "437")) &&
               !message.params[1].empty() &&
               !session->features.channel_types.contains(message.params[1].front())) {
        if (auto nick = nicknames.Retry(config.nick, message.params[1])) {
            SendControl(session, "NICK " + *nick);
        } else {
            Diagnose(session, "Nickname retries exhausted");
        }
    } else if (IsCommand(message, "001") && !message.params.empty() && !session->registered) {
        if (session->require_auth && !session->registration.Authenticated()) {
            throw Fatal{ClientError::kAuthentication};
        }
        registration.Cancel();
        session->registered = true;
        session->registered_at = detail::Now(session->clock);
        session->nick = message.params.front();
        for (const auto& channel : *session->channels) {
            SendControl(session, channel.key.empty() ? "JOIN " + channel.name
                                                     : "JOIN " + channel.name + " " + channel.key);
        }
        Invoke(session->handlers.ready, Connection(session));
    } else if (IsCommand(message, "NICK") && !message.params.empty() &&
               Equals(message.Nick(), session->nick, session->features.casemapping)) {
        session->nick = message.params.front();
    }
    if (IsCommand(message, "ERROR") && !message.params.empty()) {
        Diagnose(session, "IRC ERROR: " + std::string(message.params.back()));
    } else if (IsCommand(message, "KICK") && message.params.size() >= 2) {
        auto victim = Equals(message.params[1], session->nick, session->features.casemapping)
                          ? std::string("us")
                          : std::string(message.params[1]);
        Diagnose(session, "IRC KICK: " + std::string(message.prefix) + " removed " + victim +
                              " from " + std::string(message.params[0]) + ": " +
                              (message.params.size() > 2 ? std::string(message.params[2]) : ""));
    } else if (message.params.size() >= 3 &&
               (IsCommand(message, "437") || IsCommand(message, "471") ||
                IsCommand(message, "473") || IsCommand(message, "474") ||
                IsCommand(message, "475"))) {
        auto target = std::string(message.params[1]);
        auto reason = std::string(message.params.back());
        Diagnose(session, IsCommand(message, "437")
                              ? "IRC 437: " + target + " temporarily unavailable: " + reason
                              : "IRC JOIN " + target + " failed (" + std::string(message.command) +
                                    "): " + reason);
    }
    Invoke(session->handlers.message, Connection(session), message);
    for (const auto& route : session->handlers.subscriptions) {
        if (route.command.empty() || IsCommand(message, route.command)) {
            Invoke(route.handle, Connection(session), message);
        }
    }
}

class SocketTransport final : public Transport {
public:
    SocketTransport(asio::ip::tcp::socket socket, asio::ssl::context* tls)
        : stream_(std::move(socket), tls) {}
    asio::any_io_executor Executor() override { return stream_.get_executor(); }
    bool IsOpen() const override { return stream_.IsOpen(); }
    void Close() override {
        boost::system::error_code ignored;
        stream_.Close(ignored);
    }
    asio::awaitable<IoResult<void>> Handshake(const std::string& host, const std::string& name) override {
        try {
            co_await stream_.Handshake(host, name);
            co_return IoResult<void>{};
        } catch (const boost::system::system_error& error) {
            co_return std::unexpected(error.code());
        }
    }
    long PeerVerificationResult() override { return stream_.PeerVerificationResult(); }
    asio::awaitable<IoResult<std::size_t>> Read(asio::mutable_buffer buffer) override {
        boost::system::error_code ec;
        auto count = co_await stream_.async_read_some(buffer, asio::redirect_error(use_awaitable, ec));
        if (ec) { co_return std::unexpected(ec); }
        co_return count;
    }
    asio::awaitable<IoResult<void>> Write(asio::const_buffer buffer) override {
        boost::system::error_code ec;
        co_await asio::async_write(stream_, buffer, asio::redirect_error(use_awaitable, ec));
        if (ec) { co_return std::unexpected(ec); }
        co_return IoResult<void>{};
    }
    asio::awaitable<IoResult<void>> Shutdown() override {
        try {
            co_await stream_.Shutdown();
            co_return IoResult<void>{};
        } catch (const boost::system::system_error& error) {
            co_return std::unexpected(error.code());
        }
    }
private:
    tb::AnyStream stream_;
};

asio::awaitable<std::optional<bool>> Serve(std::unique_ptr<Transport> transport, Config& config,
                                           const Handlers& handlers, Nicknames& nicknames,
                                           const std::shared_ptr<detail::Channels>& channels) {
    const auto session_executor = transport->Executor();
    auto session = std::make_shared<Session>(Session{
        .transport = std::move(transport),
        .executor = session_executor,
        .handlers = handlers,
        .timeouts = config.timeouts,
        .clock = config.clock,
        .queue_limit = config.queued_lines,
        .features = {},
        .channels = channels,
        .registration = Registration(config.registration),
        .require_auth = config.registration.sasl && config.registration.sasl->required,
        .nick = config.nick,
        .queue = {},
        .send_interval = config.send_interval,
        .send_delay = config.send_delay,
        .pacing = {},
        .write_timer = {},
        .writable = {},
        .stop = {},
        .quit_timer = {},
        .writer_done = {},
    });
    if (config.sasl_credentials) {
        config.registration.sasl.reset();
    }
    auto executor = session->executor;
    auto idle = std::make_shared<SessionTimer>(executor, session->clock);
    std::optional<Fatal> fatal;
    bool connected = false;
    try {
        if (config.tls) {
            SessionDeadline handshake(executor, session->clock, detail::Now(session->clock) + config.timeouts.connect_timeout);
            asio::co_spawn(executor,
                           CloseOnTimeout(handshake.Wait(), session, "IRC TLS handshake timed out"),
                           asio::detached);
            try {
                auto verified = co_await session->transport->Handshake(config.server, config.tls_server_name);
                if (!verified) { throw boost::system::system_error(verified.error()); }
            } catch (const boost::system::system_error& error) {
                const auto verify = session->transport->PeerVerificationResult();
                if (verify != X509_V_OK) {
                    Diagnose(session->handlers, X509_verify_cert_error_string(verify),
                             DiagnosticKind::kTransport, Severity::kError, error.code(), {}, {}, verify);
                    throw Fatal{ClientError::kTlsTrust};
                }
                throw;
            }
        }
        connected = true;
        for (auto& line : session->registration.Start()) {
            SendControl(session, std::move(line));
        }
        if (!config.server_password.empty()) {
            std::string_view password = config.server_password;
            auto line = SerializeMessage("PASS", std::span(&password, 1));
            SendControl(session, std::move(*line));
        }
        SendControl(session, "NICK " + config.nick);
        SendControl(session, "USER " + config.username + " 0 * :" + config.realname);
        Invoke(handlers.connected, Connection(session));
        SessionDeadline registration(executor, session->clock, detail::Now(session->clock) + config.timeouts.registration_timeout);
        asio::co_spawn(
            executor,
            CloseOnTimeout(registration.Wait(), session,
                           "IRC registration timed out before welcome; closing connection"),
            asio::detached);
        // Include CRLF in the IRCv3 wire limit. Compact only after consuming a read batch.
        std::array<char, 8703> input;
        std::size_t used = 0;
        bool discarding = false;
        while (true) {
            const auto count = co_await session->transport->Read(
                asio::buffer(input.data() + used, input.size() - used));
            if (!count) { throw boost::system::system_error(count.error()); }
            if (!*count) { throw boost::system::system_error(asio::error::eof); }
            if (*count > input.size() - used) { throw Fatal{ClientError::kInvalidConfig}; }
            const std::string_view batch(input.data(), used + *count);
            std::size_t begin = 0;
            auto end = batch.find("\r\n", used == 0 ? 0 : used - 1);
            while (end != batch.npos) {
                if (discarding) {
                    discarding = false;
                } else if (auto parsed = ParseMessage(batch.substr(begin, end - begin))) {
                    const bool registered = session->registered;
                    session->last_input = detail::Now(session->clock);
                    session->probe_sent = false;
                    Handle(session, *parsed, config, nicknames, registration);
                    if (!registered && session->registered) {
                        asio::co_spawn(executor, WatchIdle(session, idle), asio::detached);
                    }
                } else {
                    Diagnose(session, "Ignoring malformed IRC message");
                }
                begin = end + 2;
                end = batch.find("\r\n", begin);
            }
            used = batch.size() - begin;
            if (used == input.size() || discarding) {
                if (!discarding) {
                    Diagnose(session, "Ignoring oversized IRC line");
                    discarding = true;
                }
                used = batch.ends_with('\r') ? 1 : 0;
                begin = batch.size() - used;
            }
            if (begin && used) {
                std::char_traits<char>::move(input.data(), input.data() + begin, used);
            }
        }
    } catch (const Fatal& error) {
        fatal = error;
    } catch (const boost::system::system_error& error) {
        if (error.code() != asio::error::operation_aborted && session->transport->IsOpen()) {
            Diagnose(session->handlers, "IRC connection lost", DiagnosticKind::kTransport,
                     Severity::kWarning, error.code());
        }
    } catch (...) {
        fatal = Fatal{ClientError::kHandler, std::current_exception()};
    }
    idle->cancel();
    session->closing = true;
    session->identity.reset();
    session->stop.request_stop();
    const auto cancellation = co_await asio::this_coro::cancellation_state;
    bool stopping = cancellation.cancelled() != asio::cancellation_type::none;
    if (stopping && connected && session->transport->IsOpen()) {
        co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation());
        session->quit_timer =
            std::make_shared<SessionTimer>(executor, session->clock, detail::Now(session->clock) + std::chrono::seconds(1));
        session->queue.clear();
        if (Queue(session, "QUIT :" + config.quit_reason, true)) {
            boost::system::error_code ignored;
            co_await session->quit_timer->async_wait(asio::redirect_error(use_awaitable, ignored));
            if (config.tls && !session->writing && session->transport->IsOpen()) {
                SessionDeadline shutdown(executor, session->clock, detail::Now(session->clock) + std::chrono::seconds(1));
                asio::co_spawn(executor,
                               CloseOnTimeout(shutdown.Wait(), session, "IRC TLS shutdown timed out"),
                               asio::detached);
                try {
                    co_await session->transport->Shutdown();
                } catch (const boost::system::system_error&) {
                    // Peers may close immediately after QUIT without sending close_notify.
                }
            }
        }
    }
    // Join the writer after closing so Run cannot finish while it still owns callback state.
    // Only disable cancellation on final shutdown; reconnects must retain the parent's slot.
    if (stopping) {
        co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation());
    }
    Close(session);
    if (session->writing) {
        session->writer_done.emplace(executor, 1);
        boost::system::error_code ec;
        try {
            co_await session->writer_done->async_receive(asio::redirect_error(use_awaitable, ec));
        } catch (const boost::system::system_error& error) {
            if (error.code() != asio::error::operation_aborted) {
                throw;
            }
        }
        if (cancellation.cancelled() != asio::cancellation_type::none) {
            stopping = true;
            co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation());
            if (session->writing) {
                co_await session->writer_done->async_receive(use_awaitable);
            }
        } else if (ec) {
            throw boost::system::system_error(ec);
        }
    }
    if (connected) {
        Invoke(handlers.disconnected, Connection(session));
    }
    session->handlers = {};
    if (fatal) {
        throw *fatal;
    }
    if (stopping) {
        co_return std::nullopt;
    }
    co_return session->registered&& detail::Now(session->clock) - session -> registered_at >=
        config.timeouts.stable_session;
}

using Tcp = asio::ip::tcp;

// A failed primary family releases the secondary immediately, even before it starts waiting.
asio::awaitable<Tcp::socket> ConnectFamily(std::vector<Tcp::endpoint> endpoints,
                                           Clock::time_point expires,
                                           std::shared_ptr<asio::steady_timer> stagger,
                                           bool secondary) {
    const auto executor = co_await asio::this_coro::executor;
    if (secondary && stagger->expiry() > Clock::now()) {
        boost::system::error_code ec;
        co_await stagger->async_wait(asio::redirect_error(use_awaitable, ec));
        const auto cancelled = co_await asio::this_coro::cancellation_state;
        if (cancelled.cancelled() != asio::cancellation_type::none) {
            throw boost::system::system_error(asio::error::operation_aborted);
        }
    }
    auto socket = std::make_shared<Tcp::socket>(executor);
    try {
        co_await tb::ConnectEndpoints(socket, endpoints, expires);
    } catch (...) {
        if (!secondary) {
            stagger->expires_at(Clock::now());
        }
        throw;
    }
    co_return std::move(*socket);
}

asio::awaitable<asio::ip::tcp::socket> Connect(const Config& config) {
    auto executor = co_await asio::this_coro::executor;
    auto resolver = std::make_shared<asio::ip::tcp::resolver>(executor);
    auto socket = std::make_shared<asio::ip::tcp::socket>(executor);
    const auto expires = Clock::now() + config.timeouts.connect_timeout;
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
    if (endpoints.empty()) {
        throw boost::system::system_error(asio::error::host_not_found);
    }
    std::vector<Tcp::endpoint> primary, secondary;
    const auto family = endpoints.begin()->endpoint().protocol();
    for (const auto& candidate : endpoints) {
        const auto ep = candidate.endpoint();
        (ep.protocol() == family ? primary : secondary).push_back(ep);
    }
    if (secondary.empty()) {
        co_await tb::ConnectEndpoints(socket, primary, expires);
        co_return std::move(*socket);
    }
    using asio::experimental::awaitable_operators::operator||;
    auto stagger = std::make_shared<asio::steady_timer>(
        executor, std::min(expires, Clock::now() + std::chrono::milliseconds(250)));
    try {
        auto winner = co_await (ConnectFamily(std::move(primary), expires, stagger, false) ||
                                ConnectFamily(std::move(secondary), expires, stagger, true));
        if (winner.index() == 0) {
            co_return std::move(std::get<0>(winner));
        }
        co_return std::move(std::get<1>(winner));
    } catch (const asio::multiple_exceptions& errors) {
        std::rethrow_exception(errors.first_exception());
    }
}

std::string_view InvalidConfigField(const Config& config) {
    auto token = [](std::string_view value) {
        return !value.empty() && value.front() != ':' &&
               std::ranges::none_of(value, [](unsigned char ch) { return ch <= 32 || ch == 127; });
    };
    auto text = [](std::string_view value) {
        return value.find_first_of(std::string_view("\r\n\0", 3)) == value.npos;
    };
    const bool valid_server =
        !config.server.empty() &&
        std::ranges::none_of(config.server, [](unsigned char ch) { return ch <= 32 || ch == 127; });
    if (!text(config.quit_reason) || config.quit_reason.size() > 504) {
        return "quit_reason";
    }
    if (!valid_server) {
        return "server";
    }
    if (!config.tls_server_name.empty() && !token(config.tls_server_name)) {
        return "tls_server_name";
    }
    if (!token(config.port)) {
        return "port";
    }
    if (!token(config.nick) || config.nick.size() > 100) {
        return "nick";
    }
    if (!token(config.username)) {
        return "username";
    }
    if (!text(config.realname) || config.username.size() + config.realname.size() + 12 > 510) {
        return "username/realname";
    }
    if (!text(config.server_password) || config.server_password.size() > 503) {
        return "server_password";
    }
    if (config.send_interval.count() < 0 || config.send_interval > std::chrono::hours(24)) {
        return "send_interval";
    }
    if (config.certificate_file.empty() != config.private_key_file.empty()) {
        return "certificate_file/private_key_file";
    }
    for (const auto& channel : config.channels) {
        if (!token(channel) || channel.size() > 505) {
            return "channels";
        }
    }
    for (const auto& server : config.fallback_servers) {
        if (server.host.empty() || std::ranges::any_of(server.host, [](unsigned char ch) {
                return ch <= 32 || ch == 127;
            }) || (!server.port.empty() && !token(server.port))) {
            return "fallback_servers";
        }
    }
    for (const auto& [channel, key] : config.channel_keys) {
        if (std::ranges::find(config.channels, channel) == config.channels.end() ||
            channel.contains(',') || !token(key) || key.contains(',') ||
            key.size() > 504 - std::min<std::size_t>(504, channel.size())) {
            return "channel_keys";
        }
    }
    const std::pair<std::string_view, std::chrono::milliseconds> timeouts[] = {
        {"timeouts.connect_timeout", config.timeouts.connect_timeout},
        {"timeouts.registration_timeout", config.timeouts.registration_timeout},
        {"timeouts.idle_timeout", config.timeouts.idle_timeout},
        {"timeouts.ping_timeout", config.timeouts.ping_timeout},
        {"timeouts.write_timeout", config.timeouts.write_timeout},
        {"timeouts.reconnect_initial", config.timeouts.reconnect_initial},
        {"timeouts.reconnect_max", config.timeouts.reconnect_max},
    };
    for (const auto& [field, timeout] : timeouts) {
        if (timeout <= std::chrono::milliseconds::zero()) {
            return field;
        }
    }
    if (!ValidateRegistration(config.registration)) {
        return "registration";
    }
    // A negotiation batch must fit without dropping credentials or control commands.
    std::size_t control_lines = std::max<std::size_t>(4, config.channels.size() + 1);
    control_lines = std::max(control_lines, config.registration.caps.size() + 2);
    if (config.registration.sasl) {
        const auto& sasl = *config.registration.sasl;
        auto bytes = sasl.authzid.size();
        if (sasl.mechanism == SaslMechanism::kPlain) {
            bytes += sasl.username.size() + sasl.password.size() + 2;
        }
        control_lines = std::max(control_lines, ((bytes + 2) / 3 * 4) / 400 + 2);
    }
    if (config.queued_lines < control_lines) {
        return "queued_lines";
    }
    if (config.registration.sasl && config.registration.sasl->mechanism == SaslMechanism::kPlain &&
        !config.tls) {
        return "registration.sasl/tls";
    }
    return {};
}
} // namespace

std::expected<void, SendError> Connection::Send(std::string_view command,
                                                std::span<const std::string_view> params,
                                                std::span<const Tag> tags) const {
    auto line = SerializeMessage(command, params, tags);
    if (!line) {
        return std::unexpected(SendError::kInvalidMessage);
    }
    auto session = session_.lock();
    if (!session || session->closing || !session->transport->IsOpen()) {
        return std::unexpected(SendError::kClosed);
    }
    if ((!tags.empty() || Equals(command, "TAGMSG", CaseMapping::kAscii)) &&
        !session->registration.Enabled().contains("message-tags")) {
        return std::unexpected(SendError::kCapabilityRequired);
    }
    return Queue(session, std::move(*line), false, PaceCommand(command));
}

std::expected<void, SendError> Connection::SendLine(std::string line) const {
    auto session = session_.lock();
    if (!session || session->closing || !session->transport->IsOpen()) {
        return std::unexpected(SendError::kClosed);
    }
    auto parsed = ParseMessage(line);
    if (!parsed || !parsed->prefix.empty() || parsed->params.size() > 15 ||
        (line.starts_with('@') && line.find(' ') + 1 > 4096)) {
        return std::unexpected(SendError::kInvalidMessage);
    }
    if (line.starts_with('@') && !ValidateClientTags(*parsed)) {
        return std::unexpected(SendError::kInvalidMessage);
    }
    if ((line.starts_with('@') || IsCommand(*parsed, "TAGMSG")) &&
        !session->registration.Enabled().contains("message-tags")) {
        return std::unexpected(SendError::kCapabilityRequired);
    }
    const bool paced = PaceCommand(parsed->command);
    return Queue(session, std::move(line), false, paced);
}

std::expected<void, SendError> Connection::Privmsg(std::string_view target,
                                                   std::string_view text) const {
    const std::string_view params[] = {target, text};
    auto line = SerializeMessage("PRIVMSG", params, {}, true);
    if (!line) {
        return std::unexpected(SendError::kInvalidMessage);
    }
    auto session = session_.lock();
    if (!session || session->closing || !session->transport->IsOpen()) {
        return std::unexpected(SendError::kClosed);
    }
    return Queue(session, std::move(*line));
}

std::stop_token Connection::StopToken() const {
    if (auto session = session_.lock()) {
        return session->stop.get_token();
    }
    std::stop_source closed;
    closed.request_stop();
    return closed.get_token();
}

std::expected<asio::any_io_executor, SendError> Connection::Executor() const {
    auto session = session_.lock();
    if (!session || session->closing) {
        return std::unexpected(SendError::kClosed);
    }
    return session->executor;
}

namespace {
asio::awaitable<std::expected<void, SendError>> WaitForSpace(std::weak_ptr<Session> weak) {
    auto session = weak.lock();
    const auto cancellation = co_await asio::this_coro::cancellation_state;
    while (session && !session->closing && session->transport->IsOpen()) {
        if (cancellation.cancelled() != asio::cancellation_type::none) {
            throw boost::system::system_error(asio::error::operation_aborted);
        }
        if (session->queue.size() < session->queue_limit) {
            co_return std::expected<void, SendError>{};
        }
        if (!session->writable) {
            session->writable.emplace(session->executor, Clock::time_point::max());
        }
        boost::system::error_code ec;
        co_await session->writable->async_wait(asio::redirect_error(use_awaitable, ec));
        if (ec && ec != asio::error::operation_aborted) {
            throw boost::system::system_error(ec);
        }
    }
    co_return std::unexpected(SendError::kClosed);
}
} // namespace

asio::awaitable<std::expected<void, SendError>> Connection::WaitWritable() const {
    return WaitForSpace(session_);
}

std::expected<void, SendError> Connection::Notice(std::string_view target, std::string_view text) const {
    return Send("NOTICE", {target, text});
}

std::expected<void, SendError> Connection::Join(std::string_view channel, std::string_view key) const {
    if (channel.empty() || channel.find_first_of(" ,:") != channel.npos ||
        key.find_first_of(" ,") != key.npos || key.starts_with(':')) {
        return std::unexpected(SendError::kInvalidMessage);
    }
    auto session = session_.lock();
    if (!session || session->closing) {
        return std::unexpected(SendError::kClosed);
    }
    auto& desired = *session->channels;
    const auto existing = std::ranges::find_if(desired, [&](const auto& entry) {
        return Equals(entry.name, channel, session->features.casemapping);
    });
    if (existing == desired.end() && desired.size() >= session->queue_limit - 1) {
        return std::unexpected(SendError::kQueueFull);
    }
    auto sent = key.empty() ? Send("JOIN", {channel}) : Send("JOIN", {channel, key});
    if (sent) {
        if (existing == desired.end()) {
            desired.push_back({std::string(channel), std::string(key)});
        } else {
            existing->key = key;
        }
    }
    return sent;
}

std::expected<void, SendError> Connection::Part(std::string_view channel, std::string_view reason) const {
    if (channel.empty() || channel.find_first_of(" ,:") != channel.npos) {
        return std::unexpected(SendError::kInvalidMessage);
    }
    auto sent = reason.empty() ? Send("PART", {channel}) : Send("PART", {channel, reason});
    if (sent) {
        auto session = session_.lock();
        std::erase_if(*session->channels, [&](const auto& entry) {
            return Equals(entry.name, channel, session->features.casemapping);
        });
    }
    return sent;
}

void Connection::Post(std::move_only_function<void(Connection)> operation) const {
    if (!operation) {
        return;
    }
    if (auto session = session_.lock()) {
        asio::post(session->executor, [connection = *this, op = std::move(operation)]() mutable {
            op(connection);
        });
    } else {
        operation(*this);
    }
}

void Connection::PostPrivmsg(std::string target, std::string text,
                             std::move_only_function<void(std::expected<void, SendError>)> completion) const {
    Post([target = std::move(target), text = std::move(text),
          completion = std::move(completion)](Connection connection) mutable {
        auto result = connection.Privmsg(target, text);
        if (completion) {
            completion(result);
        }
    });
}

bool Connection::IsOpen() const {
    auto session = session_.lock();
    return session && !session->closing && session->transport->IsOpen();
}

std::string Connection::Nick() const {
    auto session = session_.lock();
    return session ? session->nick : std::string{};
}

CaseMapping Connection::Casemapping() const {
    auto session = session_.lock();
    return session ? session->features.casemapping : CaseMapping::kRfc1459;
}

std::size_t Connection::TextBudget(std::string_view target) const {
    auto session = session_.lock();
    if (!session || session->closing) {
        return 0;
    }
    const auto bound = [&](std::string_view key, std::size_t fallback) {
        const auto& values = session->features.values;
        if (const auto found = values.find(key); found != values.end()) {
            const auto& text = found->second;
            std::size_t value = 0;
            const auto end = text.data() + text.size();
            const auto parsed = std::from_chars(text.data(), end, value);
            if (parsed.ec == std::errc{} && parsed.ptr == end && value) {
                return std::min<std::size_t>(value, 510);
            }
        }
        return fallback;
    };
    const auto prefix = session->nick.size() + 2 + bound("USERLEN", 32) + bound("HOSTLEN", 255);
    return PrivmsgTextBudget(target, prefix);
}

ServerFeatures Connection::Features() const {
    auto session = session_.lock();
    return session ? session->features : ServerFeatures{};
}

std::optional<std::string> Connection::Feature(std::string_view name) const {
    if (auto session = session_.lock()) {
        const auto& values = session->features.values;
        if (auto it = values.find(name); it != values.end()) {
            return it->second;
        }
    }
    return {};
}

std::size_t Connection::NickLength() const {
    auto session = session_.lock();
    return session ? session->features.nick_length : ServerFeatures{}.nick_length;
}

bool Connection::HasCapability(std::string_view name) const {
    auto session = session_.lock();
    return session && session->registration.Enabled().contains(name);
}

bool Connection::IsSelf(std::string_view nick) const {
    auto session = session_.lock();
    return session && Equals(nick, session->nick, session->features.casemapping);
}

std::weak_ptr<void> Connection::Identity() const {
    auto session = session_.lock();
    return session ? session->identity : std::weak_ptr<void>{};
}

asio::awaitable<std::expected<void, ClientError>> Run(Config config, Handlers handlers, Connector connector) {
    if (config.port.empty()) {
        config.port = config.tls ? "6697" : "6667";
    }
    if (config.username.empty()) {
        config.username = config.nick;
    }
    if (const auto field = InvalidConfigField(config); !field.empty()) {
        Diagnose(handlers, "Invalid IRC configuration: " + std::string(field),
                 DiagnosticKind::kConfiguration, Severity::kError);
        co_return std::unexpected(ClientError::kInvalidConfig);
    }
    auto executor = co_await asio::this_coro::executor;
    asio::ssl::context tls(asio::ssl::context::tls_client);
    try {
        if (!SSL_CTX_set_min_proto_version(tls.native_handle(), TLS1_2_VERSION)) {
            co_return std::unexpected(ClientError::kTlsConfiguration);
        }
        tls.set_default_verify_paths();
        if (!config.ca_file.empty()) {
            tls.load_verify_file(config.ca_file);
        }
        if (!config.certificate_file.empty()) {
            tls.use_certificate_chain_file(config.certificate_file);
            tls.use_private_key_file(config.private_key_file, asio::ssl::context::pem);
            if (!SSL_CTX_check_private_key(tls.native_handle())) {
                co_return std::unexpected(ClientError::kTlsConfiguration);
            }
        }
    } catch (const boost::system::system_error&) {
        co_return std::unexpected(ClientError::kTlsConfiguration);
    }
    auto channels = std::make_shared<detail::Channels>();
    for (const auto& channel : config.channels) {
        const auto key = config.channel_keys.find(channel);
        channels->push_back({channel, key == config.channel_keys.end() ? std::string{} : key->second});
    }
    Nicknames nicknames;
    const auto cancellation = co_await asio::this_coro::cancellation_state;
    const auto initial = std::min(config.timeouts.reconnect_initial, config.timeouts.reconnect_max);
    auto delay = initial;
    std::size_t retries = 0;
    std::size_t next_server = 0;
    const ServerAddress primary{config.server, config.port};
    std::mt19937 jitter(std::random_device{}());
    while (true) {
        auto failure = ClientError::kNetwork;
        try {
            const auto& server = next_server == 0 ? primary : config.fallback_servers[next_server - 1];
            config.server = server.host;
            config.port = server.port.empty() ? primary.port : server.port;
            next_server = next_server == config.fallback_servers.size() ? 0 : next_server + 1;
            if (config.sasl_credentials) {
                try {
                    config.registration.sasl = config.sasl_credentials();
                } catch (...) {
                    Diagnose(handlers, "IRC credential provider failed", DiagnosticKind::kCallback,
                             Severity::kError, {}, {}, std::current_exception());
                    co_return std::unexpected(ClientError::kHandler);
                }
                if (!config.registration.sasl) {
                    co_return std::unexpected(ClientError::kAuthentication);
                }
                if (const auto field = InvalidConfigField(config); !field.empty()) {
                    Diagnose(handlers, "Invalid IRC configuration: " + std::string(field),
                             DiagnosticKind::kConfiguration, Severity::kError);
                    co_return std::unexpected(ClientError::kInvalidConfig);
                }
            }
            std::unique_ptr<Transport> transport;
            if (connector) {
                auto connected = co_await connector(config);
                if (!connected) { throw boost::system::system_error(connected.error()); }
                transport = std::move(*connected);
                if (!transport || transport->Executor() != executor) {
                    co_return std::unexpected(ClientError::kInvalidConfig);
                }
            } else {
                auto socket = co_await Connect(config);
                transport = std::make_unique<SocketTransport>(std::move(socket), config.tls ? &tls : nullptr);
            }
            nicknames.Begin();
            auto stable = co_await Serve(std::move(transport), config, handlers, nicknames, channels);
            if (!stable) {
                co_return std::expected<void, ClientError>{};
            }
            if (*stable) {
                delay = initial;
                retries = 0;
            }
        } catch (const Fatal& error) {
            if (error.exception) {
                Diagnose(handlers, "IRC application callback failed", DiagnosticKind::kCallback,
                         Severity::kError, {}, {}, error.exception);
            }
            failure = error.error;
            if (!config.should_retry) {
                co_return std::unexpected(failure);
            }
        } catch (const boost::system::system_error& error) {
            if (cancellation.cancelled() != asio::cancellation_type::none) {
                co_return std::expected<void, ClientError>{};
            }
            Diagnose(handlers, "IRC connection failed; retrying", DiagnosticKind::kTransport,
                     Severity::kWarning, error.code());
        }
        if (cancellation.cancelled() != asio::cancellation_type::none) {
            co_return std::expected<void, ClientError>{};
        }
        if (config.sasl_credentials) {
            config.registration.sasl.reset();
        }
        if (config.should_retry) {
            try {
                if (!config.should_retry(failure, retries)) {
                    co_return std::unexpected(failure);
                }
            } catch (...) {
                Diagnose(handlers, "IRC retry policy failed", DiagnosticKind::kCallback,
                         Severity::kError, {}, {}, std::current_exception());
                co_return std::unexpected(ClientError::kHandler);
            }
        }
        if (config.max_retries && retries >= *config.max_retries) {
            co_return std::unexpected(ClientError::kRetryLimit);
        }
        ++retries;
        auto wait = delay;
        if (config.reconnect_jitter) {
            using Count = std::chrono::milliseconds::rep;
            const auto low = std::max(initial, delay / 2);
            const auto high = delay + std::min(delay / 2, config.timeouts.reconnect_max - delay);
            wait = std::chrono::milliseconds(
                std::uniform_int_distribution<Count>(low.count(), high.count())(jitter));
        }
        SessionTimer timer(executor, config.clock, detail::Now(config.clock) + wait);
        boost::system::error_code ec;
        try {
            co_await timer.async_wait(asio::redirect_error(use_awaitable, ec));
        } catch (const boost::system::system_error& error) {
            ec = error.code();
        }
        if (cancellation.cancelled() != asio::cancellation_type::none) {
            co_return std::expected<void, ClientError>{};
        }
        if (ec) {
            co_return std::unexpected(ClientError::kReconnectTimer);
        }
        delay = delay + std::min(delay, config.timeouts.reconnect_max - delay);
    }
}
} // namespace ircbot
