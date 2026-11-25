#include <argp.h>

#include <exception>
#include <string_view>
#include <utility>

#include <boost/asio.hpp>

#include <spdlog/sinks/stdout_color_sinks.h>

#include "common/log.hpp"
#include "server.hpp"

namespace asio = boost::asio;

namespace {
struct Arguments {
    bool verbose = false;
    const char* server = nullptr;
    const char* port = nullptr;
    const char* nick = nullptr;
    const char* channels = nullptr;
};

constexpr argp_option kOptions[] = {
    {
        .name = "server",
        .key = 's',
        .arg = "SERVER",
        .doc = "IRC server (required)",
    },
    {
        .name = "port",
        .key = 'p',
        .arg = "PORT",
        .doc = "IRC server port (required)",
    },
    {
        .name = "nick",
        .key = 'n',
        .arg = "NICK",
        .doc = "Bot nickname (required)",
    },
    {.name = "verbose", .key = 'v', .doc = "Enable debug logging"},
    {},
};

error_t ParseArgument(int key, char* arg, argp_state* state) {
    auto& args = *static_cast<Arguments*>(state->input);
    switch (key) {
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

    case ARGP_KEY_ARG:
        if (state->arg_num != 0) {
            return ARGP_ERR_UNKNOWN;
        }
        args.channels = arg;
        break;

    case ARGP_KEY_END:
        if (!args.server || !args.port || !args.nick || !args.channels) {
            argp_error(state, "expected -s SERVER -p PORT -n NICK CHANNEL[,CHANNEL...]");
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
        };
        asio::co_spawn(io, tb::RunBot(std::move(config)), [](const std::exception_ptr& error) {
            if (error) {
                std::rethrow_exception(error);
            }
        });
        io.run();
    } catch (const std::exception& e) {
        TB_LOG_ERROR("Bot failed: {}", e.what());
        return 1;
    }

    return 0;
}
