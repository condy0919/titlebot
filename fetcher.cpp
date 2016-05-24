#include "fetcher.hpp"
#include "utils/http.hpp"

Fetcher::Fetcher() {}

void Fetcher::start(std::string host, std::string uri,
                    std::function<void(std::string)> cb) {
    auto conn = std::make_shared<Connection>(io_service_, host, uri, cb);
    conn->start();
}

void Fetcher::run() {
    io_service_.run();
}
