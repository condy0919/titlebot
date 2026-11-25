#include <csignal>
#include <iostream>
#include <string>

#include <boost/asio.hpp>

#include "ircbot/client.hpp"

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "Usage: ircbot_echo SERVER NICK CHANNEL\n";
        return 2;
    }
    namespace asio = boost::asio;
    asio::io_context io;
    asio::cancellation_signal stop;
    asio::signal_set signals(io, SIGINT, SIGTERM);
    signals.async_wait([&](boost::system::error_code ec, int) {
        if (!ec) {
            stop.emit(asio::cancellation_type::terminal);
        }
    });
    ircbot::Config config;
    config.server = argv[1];
    config.nick = argv[2];
    config.channels = {argv[3]};
    config.tls = true;
    ircbot::Handlers handlers;
    handlers.message = [](ircbot::Connection connection, const ircbot::MessageView& message) {
        if (!ircbot::IsCommand(message, "PRIVMSG") || message.params.size() != 2 ||
            connection.IsSelf(message.Nick()) || message.params[1] != "!ping") {
            return;
        }
        auto target = connection.IsSelf(message.params[0]) ? message.Nick() : message.params[0];
        if (!connection.Privmsg(target, "pong")) {
            std::cerr << "Reply was not queued\n";
        }
    };
    handlers.diagnostic = [](std::string_view text) { std::cerr << text << '\n'; };
    int status = 0;
    asio::co_spawn(io, ircbot::Run(std::move(config), std::move(handlers)),
                   asio::bind_cancellation_slot(
                       stop.slot(), [&](std::exception_ptr error,
                                        std::expected<void, ircbot::ClientError> result) {
                           status = error || !result ? 1 : 0;
                           signals.cancel();
                           io.stop();
                       }));
    io.run();
    return status;
}
