#include "fetcher.hpp"
#include "utils/http.hpp"
#include "utils/log.hpp"
#include <boost/algorithm/string.hpp>
#include <chrono>

Fetcher::Fetcher() : timer_(io_service_, std::chrono::hours(1)) {
    timer_.async_wait(
        std::bind(&Fetcher::keepalive, this, std::placeholders::_1));
}

void Fetcher::start(std::string protocol, std::string host, std::string uri,
                    std::function<void(std::string)> cb) {
    // XXX ugly
    if (boost::iequals(protocol, "https")) {
        auto conn = std::make_shared<HTTPSConnection>(
            io_service_, std::move(protocol), std::move(host), std::move(uri),
            std::move(cb));
        conn->start();
    } else {
        auto conn = std::make_shared<HTTPConnection>(
            io_service_, std::move(protocol), std::move(host), std::move(uri),
            std::move(cb));
        conn->start();
    }
}

void Fetcher::run() {
    io_service_.run();
}

void Fetcher::poll() {
    io_service_.poll();
}

void Fetcher::keepalive(const boost::system::error_code& e
                        __attribute__((unused))) {
    timer_.expires_from_now(std::chrono::hours(24));  // XXX infinity time is better
    timer_.async_wait(
        std::bind(&Fetcher::keepalive, this, std::placeholders::_1));
}
