#include "fetcher.hpp"
#include "utils/http.hpp"
#include "utils/log.hpp"
#include <chrono>

Fetcher::Fetcher() : timer_(io_service_, std::chrono::hours(1)) {
    timer_.async_wait(
        std::bind(&Fetcher::keepalive, this, std::placeholders::_1));
}

void Fetcher::start(std::string host, std::string uri,
                    std::function<void(std::string)> cb) {
    auto conn = std::make_shared<Connection>(io_service_, std::move(host),
                                             std::move(uri), std::move(cb));
    conn->start();
}

void Fetcher::run() {
    io_service_.run();
}

void Fetcher::poll() {
    io_service_.poll();
}

void Fetcher::keepalive(const boost::system::error_code& e
                        __attribute__((unused))) {
    //DEBUG("do heartbeat!");
    timer_.expires_from_now(std::chrono::hours(1));  // XXX
    timer_.async_wait(
        std::bind(&Fetcher::keepalive, this, std::placeholders::_1));
}
