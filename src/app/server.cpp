#include "server.hpp"

#include <memory>
#include <stdexcept>
#include <utility>

#include <boost/asio/ssl/context.hpp>
#include <boost/asio/this_coro.hpp>

#include "common/log.hpp"
#include "fetch_scheduler.hpp"
#include "irc/irc.hpp"
#include "irc/policy.hpp"
#include "ircbot/client.hpp"

namespace tb {
namespace {
namespace asio = boost::asio;

struct ReplySession {
    ircbot::Connection connection;
    IrcReplyPolicy limiter;
};

void Reply(const std::shared_ptr<ReplySession>& session, const std::string& target,
           const std::string& title, const ReplyLimits& limits, FetchScheduler& scheduler) {
    if (!session->connection.IsOpen()) {
        return;
    }
    auto line = FormatReplyLine(target, title, limits.line_bytes);
    if (!line || !session->limiter.Allow(target, Clock::now(), session->connection.Casemapping())) {
        scheduler.RecordReplyDrop();
        TB_LOG_WARN("Dropping unformattable or rate-limited IRC reply");
        return;
    }
    if (!session->connection.SendLine(std::move(*line))) {
        scheduler.RecordReplyDrop();
        TB_LOG_WARN("Dropping IRC reply: connection send queue rejected the line");
    }
}

ircbot::Config ClientConfig(const BotConfig& config) {
    ircbot::Config client{
        .server = config.server,
        .port = config.port,
        .nick = config.nick,
        .username = config.nick,
        .realname = "titlebot",
        .channels = {},
        .tls = config.tls,
        .server_password = config.server_password,
        .ca_file = config.ca_file,
        .certificate_file = config.certificate_file,
        .private_key_file = config.private_key_file,
        .registration = config.registration,
        .timeouts = config.timeouts,
        .queued_lines = config.replies.queued,
    };
    std::string_view remaining(config.channels);
    do {
        const auto comma = remaining.find(',');
        client.channels.emplace_back(remaining.substr(0, comma));
        if (comma == remaining.npos) {
            break;
        }
        remaining.remove_prefix(comma + 1);
    } while (true);
    return client;
}

const char* ClientErrorText(ircbot::ClientError error) {
    switch (error) {
    case ircbot::ClientError::kInvalidConfig:
        return "Invalid IRC client configuration";
    case ircbot::ClientError::kTlsConfiguration:
        return "IRC TLS configuration failed";
    case ircbot::ClientError::kTlsTrust:
        return "IRC TLS peer verification failed";
    case ircbot::ClientError::kAuthentication:
        return "IRC authentication failed";
    case ircbot::ClientError::kReconnectTimer:
        return "IRC reconnect timer failed";
    case ircbot::ClientError::kInvalidNickname:
        return "IRC server rejected the configured nickname";
    case ircbot::ClientError::kRetryLimit:
        return "IRC reconnect limit reached";
    case ircbot::ClientError::kNetwork:
        return "IRC connection failed";
    case ircbot::ClientError::kHandler:
        return "IRC application callback failed";
    }
    return "IRC client failed";
}
} // namespace

asio::awaitable<void> RunBot(BotConfig config) {
    return RunBot(std::move(config), {});
}

asio::awaitable<void> RunBot(BotConfig config, TitleFetcher fetch) {
    auto executor = co_await asio::this_coro::executor;
    if (!fetch) {
        auto tls = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_client);
        tls->set_default_verify_paths();
        fetch = [tls, limits = config.fetch](PrivMsg message) {
            return FetchTitle(std::move(message), *tls, limits);
        };
    }
    auto scheduler = std::make_shared<FetchScheduler>(executor, std::move(fetch), config.scheduler);
    scheduler->EnableReports();
    std::shared_ptr<ReplySession> current;
    ircbot::Handlers handlers;
    handlers.connected = [&current, limits = config.replies](ircbot::Connection connection) {
        current = std::make_shared<ReplySession>(ReplySession{connection, IrcReplyPolicy(limits)});
    };
    handlers.ready = [](ircbot::Connection connection) {
        TB_LOG_INFO("IRC registered as {}", connection.Nick());
    };
    handlers.message = [&current, scheduler, limits = config.replies](
                           ircbot::Connection connection, const ircbot::MessageView& message) {
        auto parsed = parse_privmsg(message);
        if (!parsed || !current || connection.IsSelf(parsed->who)) {
            return;
        }
        auto target = connection.IsSelf(parsed->channel) ? parsed->who : parsed->channel;
        FetchScheduler::Destination destination{connection.Identity(), target, connection.Casemapping()};
        scheduler->Submit(
            std::move(*parsed),
            [weak = std::weak_ptr(current), target = std::move(target), limits,
             scheduler = std::weak_ptr(scheduler)](const std::string& title) {
                auto session = weak.lock();
                auto owner = scheduler.lock();
                if (session && owner) {
                    Reply(session, target, title, limits, *owner);
                }
            },
            std::move(destination));
    };
    handlers.disconnected = [&current, scheduler](ircbot::Connection) {
        current.reset();
        scheduler->Report();
    };
    handlers.diagnostic_event = [](const ircbot::DiagnosticEvent& event) {
        if (event.severity == ircbot::Severity::kError) {
            TB_LOG_ERROR("{}", event.message);
        } else {
            TB_LOG_WARN("{}", event.message);
        }
    };
    auto result = co_await ircbot::Run(ClientConfig(config), std::move(handlers));
    current.reset();
    scheduler->Report();
    if (!result) {
        throw std::runtime_error(ClientErrorText(result.error()));
    }
}
} // namespace tb
