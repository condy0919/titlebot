#include "fetcher.hpp"
#include "utils/http.hpp"

Fetcher::Fetcher() {}

void Fetcher::start(std::string host, std::string uri /*, callback*/) {
    auto conn = std::make_shared<Connection>(io_service_, host, uri);
    conns_.insert(conn);
    conn->start();
}

void Fetcher::run() {
    io_service_.run();
}
