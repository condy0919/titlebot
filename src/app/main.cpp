#include <argp.h>

#include <iostream>
#include <string_view>

#include <boost/asio.hpp>

#include "server.hpp"

namespace asio = boost::asio;

namespace {
struct Arguments {
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
    {},
};

error_t ParseArgument(int key, char* arg, argp_state* state) {
    auto& args = *static_cast<Arguments*>(state->input);
    switch (key) {
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
    Arguments args{};
    argp_err_exit_status = 2;
    if (argp_parse(&kParser, argc, argv, 0, nullptr, &args) != 0) {
        return 2;
    }
    try {
        asio::io_context io;
        asio::ssl::context tls(asio::ssl::context::tls_client);
        tls.set_default_verify_paths();
        auto run = asio::co_spawn(
            io,
            tb::RunBot(args.server, args.port, args.nick, args.channels,
                       [&tls](tb::PrivMsg msg) { return tb::FetchTitle(std::move(msg), tls); }),
            asio::use_future);
        io.run();
        run.get();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
