#include "client.hpp"

#include <algorithm>
#include <deque>
#include <exception>
#include <optional>
#include <random>
#include <utility>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "detail/any_stream.hpp"
#include "detail/connect.hpp"
#include "detail/deadline.hpp"

namespace ircbot {
namespace asio = boost::asio;
using asio::use_awaitable;
using Clock = std::chrono::steady_clock;
namespace detail {
struct Session {
    AnyStream transport;
    Handlers handlers;
    Timeouts timeouts;
    std::size_t queue_limit;
    ServerFeatures features;
    Registration registration;
    std::string nick;
    std::deque<std::string> queue;
    bool writing = false;
    bool closing = false;
    std::shared_ptr<void> identity = std::make_shared<char>();
    bool registered = false;
    bool probe_sent = false;
    Clock::time_point last_input{};
    Clock::time_point registered_at{};
    std::shared_ptr<asio::steady_timer> quit_timer;
    std::shared_ptr<asio::steady_timer> drain_timer;
};
} // namespace detail
namespace {
using detail::Deadline;
using detail::Session;

void Diagnose(const Handlers& handlers, std::string_view text) {
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
    boost::system::error_code ignored;
    session->transport.Close(ignored);
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
        while (!session->queue.empty()) {
            auto line = std::move(session->queue.front()) + "\r\n";
            session->queue.pop_front();
            Deadline deadline(session->transport.get_executor(),
                              Clock::now() + session->timeouts.write_timeout);
            asio::co_spawn(session->transport.get_executor(),
                           CloseOnTimeout(deadline.Wait(), session, "IRC write timed out"),
                           asio::detached);
            co_await asio::async_write(session->transport, asio::buffer(line), use_awaitable);
        }
    } catch (const std::exception&) {
        Diagnose(session, "IRC write failed");
        Close(session);
    }
    session->writing = false;
    if (session->drain_timer) {
        session->drain_timer->cancel();
    }
    if (session->quit_timer) {
        session->quit_timer->cancel();
    }
}

std::expected<void, SendError> Queue(const std::shared_ptr<Session>& session, std::string line,
                                     bool priority = false) {
    if (!session->transport.IsOpen()) {
        return std::unexpected(SendError::kClosed);
    }
    if (session->queue.size() >= session->queue_limit) {
        if (!priority) {
            return std::unexpected(SendError::kQueueFull);
        }
        session->queue.pop_back();
        Diagnose(session, "Evicting outgoing IRC line for priority send");
    }
    if (priority) {
        session->queue.push_front(std::move(line));
    } else {
        session->queue.push_back(std::move(line));
    }
    if (!session->writing) {
        session->writing = true;
        asio::co_spawn(session->transport.get_executor(), Drain(session), asio::detached);
    }
    return {};
}

// Registration lines are ordered and must not be silently lost to application backpressure.
void SendControl(const std::shared_ptr<Session>& session, std::string line) {
    if (!Queue(session, std::move(line))) {
        Diagnose(session, "IRC control queue is full; closing connection");
        Close(session);
    }
}

asio::awaitable<void> WatchIdle(std::shared_ptr<Session> session,
                                std::shared_ptr<asio::steady_timer> timer) {
    while (session->transport.IsOpen()) {
        auto interval =
            session->probe_sent ? session->timeouts.ping_timeout : session->timeouts.idle_timeout;
        timer->expires_at(session->last_input + interval);
        boost::system::error_code ec;
        co_await timer->async_wait(asio::redirect_error(use_awaitable, ec));
        if (ec || !session->transport.IsOpen()) {
            co_return;
        }
        interval =
            session->probe_sent ? session->timeouts.ping_timeout : session->timeouts.idle_timeout;
        if (Clock::now() < session->last_input + interval) {
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
        session->last_input = Clock::now();
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
};

template <class Callback, class... Args>
void Invoke(const Callback& callback, Args&&... args) {
    if (callback) {
        try {
            callback(std::forward<Args>(args)...);
        } catch (...) {
            throw Fatal{ClientError::kHandler};
        }
    }
}

void Handle(const std::shared_ptr<Session>& session, const MessageView& message,
            const Config& config, Nicknames& nicknames, Deadline& registration) {
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
        Diagnose(session, "IRC capability negotiation or SASL authentication failed");
        throw Fatal{ClientError::kAuthentication};
    }
    for (auto& line : *actions) {
        SendControl(session, std::move(line));
    }
    if (IsCommand(message, "005")) {
        session->features.Observe(message);
        nicknames.limit = session->features.nick_length;
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
        if (config.registration.sasl && config.registration.sasl->required &&
            !session->registration.authenticated()) {
            throw Fatal{ClientError::kAuthentication};
        }
        registration.Cancel();
        session->registered = true;
        session->registered_at = Clock::now();
        session->nick = message.params.front();
        for (const auto& channel : config.channels) {
            SendControl(session, "JOIN " + channel);
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
}

asio::awaitable<std::optional<bool>> Serve(asio::ip::tcp::socket socket, const Config& config,
                                           const Handlers& handlers, Nicknames& nicknames,
                                           asio::ssl::context& tls) {
    auto session = std::make_shared<Session>(Session{
        .transport = {std::move(socket), config.tls ? &tls : nullptr},
        .handlers = handlers,
        .timeouts = config.timeouts,
        .queue_limit = config.queued_lines,
        .features = {},
        .registration = Registration(config.registration),
        .nick = config.nick,
        .queue = {},
        .quit_timer = {},
        .drain_timer = {},
    });
    auto executor = session->transport.get_executor();
    auto idle = std::make_shared<asio::steady_timer>(executor);
    std::optional<ClientError> fatal;
    bool connected = false;
    try {
        if (config.tls) {
            Deadline handshake(executor, Clock::now() + config.timeouts.connect_timeout);
            asio::co_spawn(executor,
                           CloseOnTimeout(handshake.Wait(), session, "IRC TLS handshake timed out"),
                           asio::detached);
            co_await session->transport.Handshake(config.server);
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
        Deadline registration(executor, Clock::now() + config.timeouts.registration_timeout);
        asio::co_spawn(
            executor,
            CloseOnTimeout(registration.Wait(), session,
                           "IRC registration timed out before welcome; closing connection"),
            asio::detached);
        std::string input;
        while (true) {
            // IRCv3 allows 8191 bytes of tags in addition to a 512-byte ordinary line.
            auto count = co_await asio::async_read_until(
                session->transport, asio::dynamic_buffer(input, 8703), "\r\n", use_awaitable);
            auto parsed = ParseMessage(std::string_view(input).substr(0, count - 2));
            if (!parsed) {
                Diagnose(session, "Ignoring malformed IRC message");
                input.erase(0, count);
                continue;
            }
            const bool registered = session->registered;
            session->last_input = Clock::now();
            session->probe_sent = false;
            Handle(session, *parsed, config, nicknames, registration);
            input.erase(0, count);
            if (!registered && session->registered) {
                asio::co_spawn(executor, WatchIdle(session, idle), asio::detached);
            }
        }
    } catch (const Fatal& error) {
        fatal = error.error;
    } catch (const boost::system::system_error& error) {
        if (error.code() != asio::error::operation_aborted && session->transport.IsOpen()) {
            Diagnose(session, "IRC connection lost");
        }
    } catch (...) {
        fatal = ClientError::kHandler;
    }
    idle->cancel();
    session->closing = true;
    session->identity.reset();
    const auto cancellation = co_await asio::this_coro::cancellation_state;
    bool stopping = cancellation.cancelled() != asio::cancellation_type::none;
    if (stopping && connected && session->transport.IsOpen()) {
        co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation());
        session->quit_timer =
            std::make_shared<asio::steady_timer>(executor, std::chrono::seconds(1));
        session->queue.clear();
        if (Queue(session, "QUIT :Shutting down", true)) {
            boost::system::error_code ignored;
            co_await session->quit_timer->async_wait(asio::redirect_error(use_awaitable, ignored));
        }
    }
    // Join the writer after closing so Run cannot finish while it still owns callback state.
    // Only disable cancellation on final shutdown; reconnects must retain the parent's slot.
    if (stopping) {
        co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation());
    }
    session->drain_timer = std::make_shared<asio::steady_timer>(executor, Clock::time_point::max());
    Close(session);
    if (session->writing) {
        boost::system::error_code ignored;
        try {
            co_await session->drain_timer->async_wait(asio::redirect_error(use_awaitable, ignored));
        } catch (const boost::system::system_error& error) {
            if (error.code() != asio::error::operation_aborted) {
                throw;
            }
        }
        if (cancellation.cancelled() != asio::cancellation_type::none) {
            stopping = true;
            co_await asio::this_coro::reset_cancellation_state(asio::disable_cancellation());
            if (session->writing) {
                co_await session->drain_timer->async_wait(
                    asio::redirect_error(use_awaitable, ignored));
            }
        }
    }
    if (connected) {
        Invoke(handlers.disconnected, Connection(session));
    }
    session->handlers = {};
    if (fatal) {
        throw Fatal{*fatal};
    }
    if (stopping) {
        co_return std::nullopt;
    }
    co_return session->registered&& Clock::now() - session -> registered_at >=
        config.timeouts.stable_session;
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
    co_await detail::ConnectEndpoints(socket, endpoints, expires);
    co_return std::move(*socket);
}

bool ValidConfig(const Config& config) {
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
    if (!valid_server || !token(config.port) || !token(config.nick) || !token(config.username) ||
        !text(config.realname) || !text(config.server_password) || config.nick.size() > 100 ||
        config.username.size() + config.realname.size() + 12 > 510 ||
        config.server_password.size() > 503 || !config.queued_lines ||
        config.certificate_file.empty() != config.private_key_file.empty()) {
        return false;
    }
    for (const auto& channel : config.channels) {
        if (!token(channel) || channel.size() > 505) {
            return false;
        }
    }
    for (auto timeout :
         {config.timeouts.connect_timeout, config.timeouts.registration_timeout,
          config.timeouts.idle_timeout, config.timeouts.ping_timeout, config.timeouts.write_timeout,
          config.timeouts.reconnect_initial, config.timeouts.reconnect_max}) {
        if (timeout <= std::chrono::milliseconds::zero()) {
            return false;
        }
    }
    if (!ValidateRegistration(config.registration)) {
        return false;
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
        return false;
    }
    if (config.registration.sasl && config.registration.sasl->mechanism == SaslMechanism::kPlain &&
        !config.tls) {
        return false;
    }
    return true;
}
} // namespace

std::expected<void, SendError> Connection::Send(std::string_view command,
                                                std::span<const std::string_view> params,
                                                std::span<const Tag> tags) const {
    auto line = SerializeMessage(command, params, tags);
    if (!line) {
        return std::unexpected(SendError::kInvalidMessage);
    }
    return SendLine(std::move(*line));
}

std::expected<void, SendError> Connection::SendLine(std::string line) const {
    auto session = session_.lock();
    if (!session || session->closing || !session->transport.IsOpen()) {
        return std::unexpected(SendError::kClosed);
    }
    auto parsed = ParseMessage(line);
    if (!parsed || !parsed->prefix.empty() || parsed->params.size() > 15 ||
        (line.starts_with('@') && line.find(' ') + 1 > 4096)) {
        return std::unexpected(SendError::kInvalidMessage);
    }
    if (line.starts_with('@')) {
        if (parsed->tags.ends_with(';')) {
            return std::unexpected(SendError::kInvalidMessage);
        }
        std::vector<Tag> tags;
        auto remaining = parsed->tags;
        while (!remaining.empty()) {
            const auto separator = remaining.find(';');
            const auto item = remaining.substr(0, separator);
            const auto key = item.substr(0, item.find('='));
            auto value = parsed->Tag(key);
            if (!value) {
                return std::unexpected(SendError::kInvalidMessage);
            }
            tags.push_back({std::string(key), std::move(*value)});
            remaining.remove_prefix(separator == remaining.npos ? remaining.size() : separator + 1);
        }
        if (!SerializeMessage(parsed->command, {parsed->params.data(), parsed->params.size()},
                              tags)) {
            return std::unexpected(SendError::kInvalidMessage);
        }
    }
    if ((line.starts_with('@') || IsCommand(*parsed, "TAGMSG")) &&
        !session->registration.enabled().contains("message-tags")) {
        return std::unexpected(SendError::kCapabilityRequired);
    }
    return Queue(session, std::move(line));
}

std::expected<void, SendError> Connection::Privmsg(std::string_view target,
                                                   std::string_view text) const {
    const std::string_view params[] = {target, text};
    if (!SerializeMessage("PRIVMSG", params)) {
        return std::unexpected(SendError::kInvalidMessage);
    }
    return SendLine("PRIVMSG " + std::string(target) + " :" + std::string(text));
}

bool Connection::IsOpen() const {
    auto session = session_.lock();
    return session && !session->closing && session->transport.IsOpen();
}

std::string Connection::Nick() const {
    auto session = session_.lock();
    return session ? session->nick : std::string{};
}

CaseMapping Connection::casemapping() const {
    auto session = session_.lock();
    return session ? session->features.casemapping : CaseMapping::kRfc1459;
}

ServerFeatures Connection::Features() const {
    auto session = session_.lock();
    return session ? session->features : ServerFeatures{};
}

bool Connection::HasCapability(std::string_view name) const {
    auto session = session_.lock();
    return session && session->registration.enabled().contains(std::string(name));
}

bool Connection::IsSelf(std::string_view nick) const {
    auto session = session_.lock();
    return session && Equals(nick, session->nick, session->features.casemapping);
}

std::weak_ptr<void> Connection::Identity() const {
    auto session = session_.lock();
    return session ? session->identity : std::weak_ptr<void>{};
}

asio::awaitable<std::expected<void, ClientError>> Run(Config config, Handlers handlers) {
    if (config.port.empty()) {
        config.port = config.tls ? "6697" : "6667";
    }
    if (config.username.empty()) {
        config.username = config.nick;
    }
    if (!ValidConfig(config)) {
        co_return std::unexpected(ClientError::kInvalidConfig);
    }
    auto executor = co_await asio::this_coro::executor;
    asio::ssl::context tls(asio::ssl::context::tls_client);
    try {
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
    Nicknames nicknames;
    const auto cancellation = co_await asio::this_coro::cancellation_state;
    auto delay = config.timeouts.reconnect_initial;
    while (true) {
        try {
            auto socket = co_await Connect(config);
            nicknames.Begin();
            auto stable = co_await Serve(std::move(socket), config, handlers, nicknames, tls);
            if (!stable) {
                co_return std::expected<void, ClientError>{};
            }
            if (*stable) {
                delay = config.timeouts.reconnect_initial;
            }
        } catch (const Fatal& error) {
            co_return std::unexpected(error.error);
        } catch (const boost::system::system_error&) {
            if (cancellation.cancelled() != asio::cancellation_type::none) {
                co_return std::expected<void, ClientError>{};
            }
            Diagnose(handlers, "IRC connection failed; retrying");
        }
        if (cancellation.cancelled() != asio::cancellation_type::none) {
            co_return std::expected<void, ClientError>{};
        }
        asio::steady_timer timer(executor, delay);
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
        delay = std::min(config.timeouts.reconnect_max, delay * 2);
    }
}
} // namespace ircbot
