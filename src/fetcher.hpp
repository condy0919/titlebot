#pragma once

#include "connection.hpp"
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <functional>

class Fetcher {
public:
    Fetcher();

    void start(std::string protocol, std::string host, std::string uri,
               std::function<void(std::string)> cb);

    void run();

    void poll();

private:
    void keepalive(const boost::system::error_code& e);

private:
    boost::asio::io_service io_service_;
    boost::asio::steady_timer timer_;
};
