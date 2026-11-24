#pragma once

#include "connection.hpp"
#include <boost/asio.hpp>
#include <functional>

class Fetcher {
public:
    Fetcher();

    void start(std::string schema, std::string host, std::string uri,
               std::function<void(std::string)> cb);

    void run();

    void poll();

private:
    boost::asio::io_service io_service_;
    std::shared_ptr<boost::asio::io_service::work> work_;
};
