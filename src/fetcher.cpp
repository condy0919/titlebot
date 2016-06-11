#include "fetcher.hpp"
#include "utils/http.hpp"
#include "utils/log.hpp"
#include <boost/algorithm/string.hpp>
#include <chrono>

Fetcher::Fetcher()
    : work_(std::make_shared<boost::asio::io_service::work>(io_service_)) {}

void Fetcher::start(std::string protocol, std::string host, std::string uri,
                    std::function<void(std::string)> cb) {
    startConnection(io_service_, std::move(protocol), std::move(host),
                    std::move(uri), std::move(cb));
}

void Fetcher::run() {
    io_service_.run();
}

void Fetcher::poll() {
    io_service_.poll();
}
