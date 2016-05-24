#pragma once

#include "connection.hpp"

#include <boost/asio.hpp>
#include <unordered_set>
#include <functional>

class Fetcher {
public:
    Fetcher();

    void start(std::string host, std::string uri /*, callback*/);

    void run();

private:
    boost::asio::io_service io_service_;
    std::unordered_set<std::shared_ptr<Connection>> conns_;
};
