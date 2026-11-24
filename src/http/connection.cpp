#include "connection.hpp"

void startConnection(boost::asio::io_service& io_service, std::string schema,
                     std::string host, std::string uri,
                     std::function<void(std::string)> cb,
                     std::size_t redirect_cnt) {
    if (schema.size() == 5) { // https
        auto conn = std::make_shared<HTTPSConnection>(
            io_service, std::move(schema), std::move(host), std::move(uri),
            std::move(cb), redirect_cnt);
        conn->start();
    } else {
        auto conn = std::make_shared<HTTPConnection>(
            io_service, std::move(schema), std::move(host), std::move(uri),
            std::move(cb), redirect_cnt);
        conn->start();
    }
}

