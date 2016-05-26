#pragma once

#include "title_parser.hpp"
#include "utils/http.hpp"
#include <boost/asio.hpp>
#include <array>
#include <memory>

class Connection : public std::enable_shared_from_this<Connection> {
public:
    Connection(boost::asio::io_service& io_service, std::string host,
               std::string uri, std::function<void(std::string)> cb);

    void start();

private:
    void do_start();

    void resolve_handle(const boost::system::error_code& e,
                        boost::asio::ip::tcp::resolver::iterator ep_iter);

    void connect_handle(const boost::system::error_code& e);

    void write_handle(const boost::system::error_code& e);

    void read_handle(const boost::system::error_code& e,
                     std::size_t bytes_transferred);

    void read_content_handle(const boost::system::error_code& e,
                             std::size_t bytes_transferred);

private:
    boost::asio::io_service& io_service_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::ip::tcp::resolver resolver_;
    std::string host_, uri_;
    std::function<void(std::string)> callback_;

    std::array<char, 2048> buffer_;
    Http::Response::Parser parser_;
    Http::Response resp_;
    TitleParser title_parser_;
};
