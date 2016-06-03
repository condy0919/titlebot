#include "decoder.hpp"
#include "utils/log.hpp"
#include "connection.hpp"
#include "utils/http.hpp"
#include "utils/iconv.hpp"
#include <boost/bind.hpp>
#include <iostream>

Connection::Connection(boost::asio::io_service& io_service, std::string host,
                       std::string uri, std::function<void(std::string)> cb)
    : io_service_(io_service),
      socket_(io_service),
      resolver_(io_service),
      host_(std::move(host)),
      uri_(std::move(uri)),
      callback_(cb),
      title_parser_(std::move(cb))//,
      //content_decoder_(std::make_shared<ContentDecoder>(title_parser_)),
      //chunk_decoder_(std::make_shared<ChunkDecoder>(content_decoder_))
{}

void Connection::start() {
    DEBUG(__func__);
    auto self = shared_from_this();
    io_service_.post(boost::bind(&Connection::do_start, self));
}

void Connection::do_start() {
    DEBUG(__func__);
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
    DEBUG(__func__);
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
    DEBUG(__func__);
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
    DEBUG(__func__);
    if (e) {
        ERROR("async_write error " + e.message());
        return;
    }

    auto self = shared_from_this();
    boost::asio::async_read(
        socket_, boost::asio::buffer(buffer_, buffer_.size()),
        boost::bind(&Connection::read_header_handle, self,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));
}

void Connection::read_header_handle(const boost::system::error_code& e,
                                    std::size_t bytes_transferred) {
    DEBUG(__func__);
    if (e && bytes_transferred == 0) {
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
        // non-text/html
        std::string content_type = resp_.getHeader("Content-Type");
        if (content_type.empty()) {
            return;
        } else if (content_type.compare(0, sizeof("text/html") - 1,
                                        "text/html")) {
            callback_(content_type);
            return;
        }

        // text/html
        // set charset converter if needed
        // for example:
        // Content-Type: text/html; charset=gb2312
        {
            static char charset[] = "charset";
            static char sp[] = " ";
            auto iter = std::search(content_type.begin(), content_type.end(),
                                    charset, charset + sizeof(charset) - 1);
            if (iter != content_type.end()) {
                auto st = iter + sizeof(charset); // include '='
                auto ed =
                    std::find_first_of(st, content_type.end(), sp, sp + 1);
                std::string codec(st, ed);
                trim_left_if(codec, boost::is_any_of("\"\'"));
                trim_right_if(codec, boost::is_any_of("\"\'"));
                DEBUG("charset=" + codec);
                title_parser_.setConverter(std::make_unique<iconvpp::converter>(
                    "UTF-8", std::move(codec)));
            }
        }

        // process Transfer-Encoding and Content-Encoding
        {
            std::string encoding = resp_.getHeader("Content-Encoding");
            if (encoding.empty()) {
                content_decoder_ = std::make_shared<ContentDecoder>(title_parser_);
            } else if (encoding == "gzip") {
                content_decoder_ = std::make_shared<GzipDecoder>(title_parser_);
            } else if (encoding == "deflate") {
                content_decoder_ = std::make_shared<DeflateDecoder>(title_parser_);
            } else {
                ERROR("un-implement encoding = " + encoding);
                return;
            }
        }
        chunk_decoder_ = std::make_shared<ChunkDecoder>(content_decoder_);
        if (resp_.getHeader("Transfer-Encoding") == "chunked") {
            chunk_decoder_->setParser(std::make_unique<Http::Chunk::Parser>());
        }

        if (!chunk_decoder_->parse(iter, buffer_.data() + bytes_transferred)) {
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
            boost::bind(&Connection::read_header_handle, self,
                        boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred));
    }
}

void Connection::read_content_handle(const boost::system::error_code& e,
                                     std::size_t bytes_transferred) {
    DEBUG(__func__);
    if (e && bytes_transferred == 0) {
        ERROR("read content handle error " + e.message());
        return;
    }

    auto self = shared_from_this();
    if (!chunk_decoder_->parse(buffer_.data(),
                               buffer_.data() + bytes_transferred)) {
        // not found, read content and parse it
        boost::asio::async_read(
            socket_, boost::asio::buffer(buffer_, buffer_.size()),
            boost::bind(&Connection::read_content_handle, self,
                        boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred));
    }
}
