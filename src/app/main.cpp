#include <iostream>

#include <boost/asio.hpp>

#include "server.hpp"

namespace asio = boost::asio;

int main(int argc, char** argv) {
    if (argc != 5 || !*argv[1] || !*argv[2] || !*argv[3] || !*argv[4] ||
        std::string_view(argv[3]).find_first_of(" \r\n") != std::string_view::npos ||
        std::string_view(argv[4]).find_first_of(" \r\n") != std::string_view::npos) {
        std::cerr << "Usage: titlebot SERVER PORT NICK CHANNEL[,CHANNEL...]\n";
        return 2;
    }
    try {
        asio::io_context io;
        asio::ssl::context tls(asio::ssl::context::tls_client);
        tls.set_default_verify_paths();
        auto run = asio::co_spawn(
            io,
            tb::RunBot(argv[1], argv[2], argv[3], argv[4],
                       [&tls](tb::PrivMsg msg) { return tb::FetchTitle(std::move(msg), tls); }),
            asio::use_future);
        io.run();
        run.get();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
