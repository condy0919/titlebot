#pragma once

#include <boost/asio/ssl.hpp>

namespace ssl {
class Context {
public:
    Context();

    boost::asio::ssl::context& native();

private:
    boost::asio::ssl::context ctx_;
};
}
