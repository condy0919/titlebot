#include "server.hpp"
#include <boost/asio.hpp>
#include <iostream>

namespace asio = boost::asio;

asio::awaitable<void> Run(char** args, asio::ssl::context& tls) {
    auto executor = co_await asio::this_coro::executor;
    asio::ip::tcp::resolver resolver(executor);
    unsigned delay = 1;
    while (true) {
        try {
            auto endpoints = co_await resolver.async_resolve(args[1], args[2], asio::use_awaitable);
            asio::ip::tcp::socket socket(executor);
            co_await asio::async_connect(socket, endpoints, asio::use_awaitable);
            co_await tb::RunSession(std::move(socket), args[3], args[4], [&tls](tb::PrivMsg msg) {
                return tb::FetchTitle(std::move(msg), tls);
            });
        } catch (const boost::system::system_error&) {
            std::cerr << "IRC connection failed\n";
        }
        asio::steady_timer timer(executor, std::chrono::seconds(delay));
        co_await timer.async_wait(asio::use_awaitable);
        delay = std::min(60u, delay * 2);
    }
}

int main(int argc, char** argv) {
    if (argc != 5 || std::string_view(argv[3]).find_first_of(" \r\n") != std::string_view::npos ||
        std::string_view(argv[4]).find_first_of(" \r\n") != std::string_view::npos) {
        std::cerr << "Usage: titlebot SERVER PORT NICK CHANNEL[,CHANNEL...]\n";
        return 2;
    }
    try {
        asio::io_context io;
        asio::ssl::context tls(asio::ssl::context::tls_client);
        tls.set_default_verify_paths();
        auto run = asio::co_spawn(io, Run(argv, tls), asio::use_future);
        io.run();
        run.get();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
