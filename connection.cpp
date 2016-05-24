#include "connection.hpp"
#include "utils/log.hpp"
#include "utils/http.hpp"
#include <boost/bind.hpp>
#include <iostream>

Connection::Connection(boost::asio::io_service& io_service, std::string host,
                       std::string uri)
    : io_service_(io_service),
      socket_(io_service),
      resolver_(io_service),
      host_(std::move(host)),
      uri_(std::move(uri)) {}


void Connection::start() {
    boost::asio::ip::tcp::resolver::query query(host_, "http"); // protocol
    auto self = shared_from_this();
    resolver_.async_resolve(query,
                            boost::bind(&Connection::resolve_handle, self,
                                        boost::asio::placeholders::error,
                                        boost::asio::placeholders::iterator));
}

void Connection::resolve_handle(
    const boost::system::error_code& e,
    boost::asio::ip::tcp::resolver::iterator ep_iter) {
    if (e) {
        ERROR("resolve error " + e.message());
        return;
    }

    auto self = shared_from_this();
    boost::asio::async_connect(socket_, ep_iter,
                               boost::bind(&Connection::connect_handle, self,
                                           boost::asio::placeholders::error));
}

void Connection::connect_handle(const boost::system::error_code& e) {
    if (e) {
        ERROR("connect error " + e.message());
        return;
    }

    auto self = shared_from_this();
    auto req = Http::Request::get(self->host_, self->uri_);
    boost::asio::async_write(socket_, boost::asio::buffer(req, req.size()),
                             boost::bind(&Connection::write_handle, self,
                                         boost::asio::placeholders::error));
}

void Connection::write_handle(const boost::system::error_code& e) {
    if (e) {
        ERROR("async_write error " + e.message());
        return;
    }

    auto self = shared_from_this();
    boost::asio::async_read(
        socket_, boost::asio::buffer(buffer_, buffer_.size()),
        boost::bind(&Connection::read_handle, self,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));
}

void Connection::read_handle(const boost::system::error_code& e,
                             std::size_t bytes_transferred) {
    if (e) {
        ERROR("async_read error " + e.message());
        return;
    }

    auto self = shared_from_this();
    decltype(buffer_)::iterator iter;
    Http::Response::Parser::state st;
    std::tie(st, iter) = parser_.parse(resp_, buffer_.data(),
                                       buffer_.data() + bytes_transferred);
    if (st == Http::Response::Parser::state::bad) {
        // close socket
    } else if (st == Http::Response::Parser::state::good) {
        // start parsing&reading http content body
        // [iter, buffer_.data() + bytes_transferred)
        std::experimental::optional<std::string> opt =
            title_parser_.parse(iter, buffer_.data() + bytes_transferred);
        if (opt) {
            std::cout << "Found! " << opt.value() << std::endl;
        } else {
            // not found, read content and parse it
            boost::asio::async_read(
                socket_, boost::asio::buffer(buffer_, buffer_.size()),
                boost::bind(&Connection::read_content_handle, self,
                            boost::asio::placeholders::error,
                            boost::asio::placeholders::bytes_transferred));
        }
    } else {
        // read again
        boost::asio::async_read(
            socket_, boost::asio::buffer(buffer_, buffer_.size()),
            boost::bind(&Connection::read_handle, self,
                        boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred));
    }
}

void Connection::read_content_handle(const boost::system::error_code& e,
                                     std::size_t bytes_transferred) {
    if (e) {
        ERROR("read content handle error " + e.message());
        return;
    }

    auto self = shared_from_this();
    std::experimental::optional<std::string> opt =
        title_parser_.parse(buffer_.data(), buffer_.data() + bytes_transferred);
    if (opt) {
        std::cout << "Found! " << opt.value() << std::endl;
    } else {
        // not found, read content and parse it
        boost::asio::async_read(
            socket_, boost::asio::buffer(buffer_, buffer_.size()),
            boost::bind(&Connection::read_content_handle, self,
                        boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred));
    }
}
