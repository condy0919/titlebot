#ifndef IRCBOT_CLIENT_HPP_
#define IRCBOT_CLIENT_HPP_

#include <chrono>
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
#include <utility>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/system/error_code.hpp>

#include "features.hpp"
#include "message.hpp"
#include "registration.hpp"

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
    bool reconnect_jitter = true;
    std::optional<std::size_t> max_retries{};
    std::map<std::string, std::string> channel_keys;
};

enum class SendError { kClosed, kInvalidMessage, kQueueFull, kCapabilityRequired };
enum class ClientError { kInvalidConfig, kTlsConfiguration, kTlsTrust, kAuthentication, kHandler, kReconnectTimer, kRetryLimit, kInvalidNickname };

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
    bool IsOpen() const;
    std::string Nick() const;
    /// For example, TextBudget("#room") reserves the server-advertised user/host limits
    /// and the current nickname when computing a forwarded PRIVMSG budget.
    std::size_t TextBudget(std::string_view target) const;
    CaseMapping casemapping() const;
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
};

struct Handlers {
    // Callbacks run on Run's executor and must not block. Message views last only for the call;
    // copy fields needed by a co_spawn task, and retain Connection to isolate late replies.
    std::function<void(Connection)> connected;
    std::function<void(Connection)> ready;
    std::function<void(Connection, const MessageView&)> message;
    std::function<void(Connection)> disconnected;
    std::function<void(std::string_view)> diagnostic;
    std::function<void(const DiagnosticEvent&)> diagnostic_event;
};

/// Run one independent client on a single-threaded executor (or a strand).
/// For example, Run({.server="irc.example.org", .nick="bot", .tls=true}, handlers)
/// negotiates capabilities, registers, and dispatches messages until cancelled.
/// Network failures reconnect; invalid configuration, required SASL failure, and non-diagnostic
/// callback exceptions return an error. Terminal cancellation sends QUIT before completing
/// successfully.
boost::asio::awaitable<std::expected<void, ClientError>> Run(Config config, Handlers handlers = {});

} // namespace ircbot

#endif // IRCBOT_CLIENT_HPP_
