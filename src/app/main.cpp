#include <argp.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <csignal>
#include <cstdlib>
#include <exception>
#include <string_view>
#include <utility>

#include <boost/asio.hpp>

#include "common/log.hpp"
#include "server.hpp"

namespace asio = boost::asio;

namespace {
struct Arguments {
    bool tls = false;
    bool verbose = false;
    const char* server = nullptr;
    const char* port = nullptr;
    const char* nick = nullptr;
    const char* channels = nullptr;
    const char* sasl_user = nullptr;
    const char* sasl_password_env = "TITLEBOT_SASL_PASSWORD";
    bool sasl_external = false;
    const char* server_password_env = nullptr;
    const char* ca_file = nullptr;
    const char* certificate_file = nullptr;
    const char* private_key_file = nullptr;
};

enum Option {
    kSaslUser = 256,
    kSaslPasswordEnv,
    kSaslExternal,
    kServerPasswordEnv,
    kCaFile,
    kCertificateFile,
    kPrivateKeyFile,
};

constexpr argp_option kOptions[] = {
    {"server", 's', "SERVER", 0, "IRC server (required)", 0},
    {"port", 'p', "PORT", 0, "IRC server port (defaults to 6697 with --tls)", 0},
    {"nick", 'n', "NICK", 0, "Bot nickname (required)", 0},
    {"tls", 't', nullptr, 0, "Connect to IRC over TLS (default port 6697)", 0},
    {"verbose", 'v', nullptr, 0, "Enable debug logging", 0},
    {"sasl-user", kSaslUser, "USER", 0, "Require SASL PLAIN authentication over TLS", 0},
    {"sasl-password-env", kSaslPasswordEnv, "NAME", 0,
     "Read SASL password from NAME (default TITLEBOT_SASL_PASSWORD)", 0},
    {"sasl-external", kSaslExternal, nullptr, 0, "Require SASL EXTERNAL authentication", 0},
    {"server-password-env", kServerPasswordEnv, "NAME", 0, "Read IRC PASS password from NAME", 0},
    {"ca-file", kCaFile, "FILE", 0, "Additional IRC TLS CA certificate file", 0},
    {"cert-file", kCertificateFile, "FILE", 0, "IRC TLS client certificate chain", 0},
    {"key-file", kPrivateKeyFile, "FILE", 0, "IRC TLS client private key", 0},
    {},
};

error_t ParseArgument(int key, char* arg, argp_state* state) {
    auto& args = *static_cast<Arguments*>(state->input);
    switch (key) {
    case 't':
        args.tls = true;
        return 0;

    case 'v':
        args.verbose = true;
        return 0;

    case 's':
        args.server = arg;
        break;

    case 'p':
        args.port = arg;
        break;

    case 'n':
        args.nick = arg;
        break;

    case kSaslUser:
        args.sasl_user = arg;
        break;
    case kSaslPasswordEnv:
        args.sasl_password_env = arg;
        break;
    case kSaslExternal:
        args.sasl_external = true;
        return 0;
    case kServerPasswordEnv:
        args.server_password_env = arg;
        break;
    case kCaFile:
        args.ca_file = arg;
        break;
    case kCertificateFile:
        args.certificate_file = arg;
        break;
    case kPrivateKeyFile:
        args.private_key_file = arg;
        break;

    case ARGP_KEY_ARG:
        if (state->arg_num != 0) {
            return ARGP_ERR_UNKNOWN;
        }
        args.channels = arg;
        break;

    case ARGP_KEY_END:
        if (args.tls && !args.port) {
            args.port = "6697";
        }
        if (!args.server || !args.port || !args.nick || !args.channels) {
            argp_error(state, "expected -s SERVER -p PORT -n NICK CHANNEL[,CHANNEL...]");
        }
        if (args.sasl_user && args.sasl_external) {
            argp_error(state, "choose either --sasl-user or --sasl-external");
        }
        if ((args.sasl_user || args.sasl_external) && !args.tls) {
            argp_error(state, "SASL authentication requires --tls");
        }
        if (args.sasl_user) {
            const char* password = std::getenv(args.sasl_password_env);
            if (!password || !*password) {
                argp_error(state, "SASL password environment variable is missing or empty");
            }
        }
        if (args.server_password_env) {
            const char* password = std::getenv(args.server_password_env);
            if (!password || !*password) {
                argp_error(state, "server password environment variable is missing or empty");
            }
        }
        return 0;

    default:
        return ARGP_ERR_UNKNOWN;
    }

    const std::string_view value(arg);
    if (value.empty()) {
        argp_error(state, "argument values must not be empty");
    }
    if ((key == 'n' || key == ARGP_KEY_ARG) && value.find_first_of(" \r\n") != value.npos) {
        argp_error(state, "nickname and channels cannot contain spaces or line breaks");
    }
    return 0;
}

constexpr argp kParser{
    .options = kOptions,
    .parser = ParseArgument,
    .args_doc = "CHANNEL[,CHANNEL...]",
    .doc = "Join IRC channels and reply with web page titles.",
    .children = nullptr,
    .help_filter = nullptr,
    .argp_domain = nullptr,
};
} // namespace

int main(int argc, char** argv) {
    spdlog::set_default_logger(spdlog::stderr_color_mt("titlebot"));
    Arguments args{};
    argp_err_exit_status = 2;
    if (argp_parse(&kParser, argc, argv, 0, nullptr, &args) != 0) {
        return 2;
    }

    if (args.verbose) {
        spdlog::set_level(spdlog::level::debug);
        TB_LOG_DEBUG("Debug logging enabled");
    }
    try {
        asio::io_context io;
        tb::BotConfig config{
            .server = args.server,
            .port = args.port,
            .nick = args.nick,
            .channels = args.channels,
            .tls = args.tls,
        };
        if (args.sasl_user) {
            config.registration.sasl = ircbot::SaslConfig{
                .username = args.sasl_user,
                .password = std::getenv(args.sasl_password_env),
                .authzid = {},
            };
        } else if (args.sasl_external) {
            config.registration.sasl = ircbot::SaslConfig{
                .mechanism = ircbot::SaslMechanism::kExternal,
                .username = {},
                .password = {},
                .authzid = {},
            };
        }
        if (args.server_password_env) {
            config.server_password = std::getenv(args.server_password_env);
        }
        config.ca_file = args.ca_file ? args.ca_file : "";
        config.certificate_file = args.certificate_file ? args.certificate_file : "";
        config.private_key_file = args.private_key_file ? args.private_key_file : "";
        asio::signal_set signals(io, SIGINT, SIGTERM);
        asio::cancellation_signal cancel;
        bool stopping = false;
        signals.async_wait([&](boost::system::error_code ec, int) {
            if (!ec) {
                stopping = true;
                cancel.emit(asio::cancellation_type::terminal);
            }
        });
        asio::co_spawn(io, tb::RunBot(std::move(config)),
                       asio::bind_cancellation_slot(cancel.slot(), [&](std::exception_ptr error) {
                           signals.cancel();
                           io.stop();
                           if (error) {
                               try {
                                   std::rethrow_exception(error);
                               } catch (const boost::system::system_error& ex) {
                                   if (!stopping || ex.code() != asio::error::operation_aborted) {
                                       throw;
                                   }
                               }
                           }
                       }));
        io.run();
    } catch (const std::exception& e) {
        TB_LOG_FATAL("Bot failed: {}", e.what());
    }

    return 0;
}
