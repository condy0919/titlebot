#ifndef IRCBOT_CLIENT_HPP_
#define IRCBOT_CLIENT_HPP_

#include <chrono>
#include <concepts>
#include <cstddef>
#include <exception>
#include <expected>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/system/error_code.hpp>

#include "features.hpp"
#include "manual_clock.hpp"
#include "message.hpp"
#include "registration.hpp"
#include "transport.hpp"

namespace ircbot {
namespace detail {
struct Session;
}

struct Timeouts {
    std::chrono::milliseconds registration_timeout = std::chrono::seconds(30);
    std::chrono::milliseconds connect_timeout = std::chrono::seconds(15);
    std::chrono::milliseconds idle_timeout = std::chrono::seconds(90);
    std::chrono::milliseconds ping_timeout = std::chrono::seconds(30);
    std::chrono::milliseconds stable_session = std::chrono::minutes(1);
    std::chrono::milliseconds write_timeout = std::chrono::seconds(10);
    std::chrono::milliseconds reconnect_initial = std::chrono::seconds(1);
    std::chrono::milliseconds reconnect_max = std::chrono::seconds(60);
};

struct ServerAddress {
    std::string host;
    std::string port;
};

enum class ClientError { kNetwork, kInvalidConfig, kTlsConfiguration, kTlsTrust, kAuthentication, kHandler, kReconnectTimer, kRetryLimit, kInvalidNickname };

struct Config {
    std::string server;
    std::string port;
    std::string nick;
    std::string username;
    std::string realname = "ircbot";
    std::vector<std::string> channels;
    bool tls = false;
    std::string server_password;
    std::string ca_file;
    std::string certificate_file;
    std::string private_key_file;
    RegistrationConfig registration;
    Timeouts timeouts;
    std::size_t queued_lines = 64;
    std::chrono::milliseconds send_interval{0};
    // Return the minimum interval before the next ordinary line. Protocol control bypasses pacing.
    std::function<std::chrono::milliseconds(std::string_view)> send_delay;
    bool reconnect_jitter = true;
    std::optional<std::size_t> max_retries{};
    // For example, return false for kTlsTrust to stop after a failed verification.
    std::function<bool(ClientError, std::size_t)> should_retry;
    std::map<std::string, std::string> channel_keys;
    std::vector<ServerAddress> fallback_servers;
    std::string tls_server_name;
    std::shared_ptr<ManualClock> clock;
    // Obtain fresh SASL credentials on the session executor before each connection attempt.
    std::function<std::optional<SaslConfig>()> sasl_credentials;
    std::string quit_reason = "Shutting down";
};

enum class SendError { kClosed, kInvalidMessage, kQueueFull, kCapabilityRequired };


/// A weak handle to one connection. Copies cannot send on a later reconnect.
class Connection {
public:
    Connection() = default;
    explicit Connection(std::weak_ptr<detail::Session> session) : session_(std::move(session)) {}

    /// For example, Send("PRIVMSG", {"#room", "hello world"}) queues "PRIVMSG #room :hello world".
    std::expected<void, SendError> Send(std::string_view command,
                                        std::span<const std::string_view> params = {},
                                        std::span<const Tag> tags = {}) const;
    std::expected<void, SendError> Send(std::string_view command,
                                        std::initializer_list<std::string_view> params,
                                        std::span<const Tag> tags = {}) const {
        return Send(command, std::span(params.begin(), params.size()), tags);
    }
    /// Queue one validated line without CRLF, e.g. SendLine("NOTICE #room :hello").
    std::expected<void, SendError> SendLine(std::string line) const;
    /// For example, Privmsg("#room", "hello") queues a single channel message.
    std::expected<void, SendError> Privmsg(std::string_view target, std::string_view text) const;
    /// For example, save StopToken() in a task and check stop_requested() before replying.
    std::stop_token StopToken() const;
    /// For example, post a reply to *Executor() while the session exists.
    std::expected<boost::asio::any_io_executor, SendError> Executor() const;
    /// Await queue capacity on the session executor. For example, after kQueueFull,
    /// co_await connection.WaitWritable() before retrying Send; other senders can win the slot.
    boost::asio::awaitable<std::expected<void, SendError>> WaitWritable() const;
    /// For example, Notice("bot", "hi") sends a NOTICE with target and text.
    std::expected<void, SendError> Notice(std::string_view target, std::string_view text) const;
    /// For example, Join("#room", "secret") queues "JOIN #room secret".
    std::expected<void, SendError> Join(std::string_view channel, std::string_view key = {}) const;
    /// For example, Part("#room", "going away") queues "PART #room :going away".
    std::expected<void, SendError> Part(std::string_view channel, std::string_view reason = {}) const;
    /// For example, Post([](Connection c) { c.Send("PING", {"id"}); }) runs on the
    /// session executor. An expired session invokes the callback inline with a closed handle.
    void Post(std::move_only_function<void(Connection)> operation) const;
    /// For example, PostPrivmsg("#room", "done", callback) dispatches a send from a worker
    /// thread and invokes callback with the enqueue result. Closed handles may complete inline.
    void PostPrivmsg(std::string target, std::string text,
                     std::move_only_function<void(std::expected<void, SendError>)> completion) const;
    bool IsOpen() const;
    std::string Nick() const;
    /// For example, TextBudget("#room") reserves the server-advertised user/host limits
    /// and the current nickname when computing a forwarded PRIVMSG budget.
    std::size_t TextBudget(std::string_view target) const;
    CaseMapping Casemapping() const;
    CaseMapping casemapping() const { return Casemapping(); }
    ServerFeatures Features() const;
    /// For example, Feature("NETWORK") returns the current 005 value when advertised.
    std::optional<std::string> Feature(std::string_view name) const;
    std::size_t NickLength() const;
    bool HasCapability(std::string_view name) const;
    /// For example, IsSelf("BOT") is true when the current nickname is "bot".
    bool IsSelf(std::string_view nick) const;
    std::weak_ptr<void> Identity() const;

private:
    std::weak_ptr<detail::Session> session_;
};

enum class Severity { kWarning, kError };
enum class DiagnosticKind { kProtocol, kTransport, kRegistration, kCallback, kConfiguration };

struct DiagnosticEvent {
    Severity severity;
    DiagnosticKind kind;
    std::string_view message;
    boost::system::error_code code;
    std::optional<RegistrationError> registration;
    std::exception_ptr exception;
    std::optional<long> certificate_error;
};

template <class... Args>
class Callback {
public:
    Callback() = default;
    template <class Fn>
        requires (!std::same_as<std::remove_cvref_t<Fn>, Callback> &&
                  std::constructible_from<std::move_only_function<void(Args...)>, Fn>)
    Callback(Fn&& fn)
        : function_(std::make_shared<std::move_only_function<void(Args...)>>(std::forward<Fn>(fn))) {}
    explicit operator bool() const { return function_ && static_cast<bool>(*function_); }
    void operator()(Args... args) const { (*function_)(std::forward<Args>(args)...); }
private:
    std::shared_ptr<std::move_only_function<void(Args...)>> function_;
};

struct MessageSubscription {
    // An empty command receives all messages. The view lasts for the callback invocation.
    std::string command;
    Callback<Connection, const MessageView&> handle;
};

struct Handlers {
    // Callbacks run on Run's executor and must not block. Message views last only for the call;
    // copy fields needed by a co_spawn task, and retain Connection to isolate late replies.
    Callback<Connection> connected;
    Callback<Connection> ready;
    Callback<Connection, const MessageView&> message;
    Callback<Connection> disconnected;
    Callback<std::string_view> diagnostic;
    Callback<const DiagnosticEvent&> diagnostic_event;
    Callback<Connection, const MessageView&> capabilities_changed;
    Callback<Connection, const MessageView&> isupport_changed;
    std::vector<MessageSubscription> subscriptions;
};

// A connector supplies a connected transport on Run's executor and observes config.timeouts.
// For example, a test may return an in-memory transport seeded with "PING :test\r\n".
using Connector = std::function<boost::asio::awaitable<IoResult<std::unique_ptr<Transport>>>(const Config&)>;

/// Run one independent client on a single-threaded executor (or a strand).
/// For example, Run({.server="irc.example.org", .nick="bot", .tls=true}, handlers)
/// negotiates capabilities, registers, and dispatches messages until cancelled.
/// Network failures reconnect; invalid configuration, required SASL failure, and non-diagnostic
/// callback exceptions return an error. Terminal cancellation sends QUIT before completing
/// successfully.
boost::asio::awaitable<std::expected<void, ClientError>> Run(Config config, Handlers handlers = {}, Connector connector = {});

} // namespace ircbot

#endif // IRCBOT_CLIENT_HPP_
