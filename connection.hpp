#pragma once

#include "global.hpp"
#include "decoder.hpp"
#include "utils/log.hpp"
#include "utils/http.hpp"
#include "utils/iconv.hpp"
#include "utils/format.hpp"
#include "title_parser.hpp"
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/algorithm/string.hpp>
#include <type_traits>
#include <memory>
#include <array>

namespace TypeTraits {
template <typename T>
struct is_ssl : std::false_type {};

template <>
struct is_ssl<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>
    : std::true_type {};
};

void startConnection(boost::asio::io_service& io_service, std::string protocol,
                     std::string host, std::string uri,
                     std::function<void(std::string)> cb);


template <typename SocketT>
class Connection : public std::enable_shared_from_this<Connection<SocketT>> {
public:
    template <typename T = SocketT,
              std::enable_if_t<TypeTraits::is_ssl<T>::value, int> = 0>
    Connection(boost::asio::io_service& io_service, std::string protocol,
               std::string host, std::string uri,
               std::function<void(std::string)> cb)
        : io_service_(io_service),
          socket_(io_service, Global::getSSLContext().native()),
          resolver_(io_service),
          protocol_(std::move(protocol)),
          host_(std::move(host)),
          uri_(std::move(uri)),
          callback_(cb),
          title_parser_(std::move(cb)) {}

    template <typename T = SocketT,
              std::enable_if_t<!TypeTraits::is_ssl<T>::value, int> = 0>
    Connection(boost::asio::io_service& io_service, std::string protocol,
               std::string host, std::string uri,
               std::function<void(std::string)> cb)
        : io_service_(io_service),
          socket_(io_service),
          resolver_(io_service),
          protocol_(std::move(protocol)),
          host_(std::move(host)),
          uri_(std::move(uri)),
          callback_(cb),
          title_parser_(std::move(cb)) {}

    void start() {
        DEBUG(__func__);
        auto self = this->shared_from_this();
        io_service_.post(boost::bind(&Connection::do_start, self));
    }

private:
    void do_start() {
        DEBUG(__func__);
        auto self = this->shared_from_this();
        boost::asio::ip::tcp::resolver::query query(host_, protocol_);
        resolver_.async_resolve(
            query, boost::bind(&Connection::resolve_handle<SocketT>, self,
                               boost::asio::placeholders::error,
                               boost::asio::placeholders::iterator));
    }

    // ssl version
    template <typename T = SocketT,
              std::enable_if_t<TypeTraits::is_ssl<T>::value, int> = 0>
    void resolve_handle(const boost::system::error_code& e,
                        boost::asio::ip::tcp::resolver::iterator ep_iter) {
        DEBUG(__func__);
        if (e) {
            ERROR("resolve error " + e.message());
            return;
        }

        auto self = this->shared_from_this();
        boost::asio::async_connect(
            socket_.lowest_layer(), ep_iter,
            boost::bind(&Connection::connect_handle<T>, self,
                        boost::asio::placeholders::error));
    }

    // normal version
    template <typename T = SocketT,
              std::enable_if_t<!TypeTraits::is_ssl<T>::value, int> = 0>
    void resolve_handle(const boost::system::error_code& e,
                        boost::asio::ip::tcp::resolver::iterator ep_iter) {
        DEBUG(__func__);
        if (e) {
            ERROR("resolve error " + e.message());
        }

        auto self = this->shared_from_this();
        boost::asio::async_connect(
            socket_, ep_iter, boost::bind(&Connection::connect_handle<T>, self,
                                          boost::asio::placeholders::error));
    }

    // ssl version
    template <typename T = SocketT,
              std::enable_if_t<TypeTraits::is_ssl<T>::value, int> = 0>
    void connect_handle(const boost::system::error_code& e) {
        DEBUG(__func__);
        if (e) {
            ERROR("connect error " + e.message());
            return;
        }

        auto self = this->shared_from_this();
        socket_.set_verify_mode(boost::asio::ssl::verify_none);
        socket_.set_verify_callback(
            boost::asio::ssl::rfc2818_verification(host_));
        socket_.async_handshake(
            boost::asio::ssl::stream_base::client,
            boost::bind(&Connection::handshake_handle<T>, self,
                        boost::asio::placeholders::error));
    }

    // normal version
    template <typename T = SocketT,
              std::enable_if_t<!TypeTraits::is_ssl<T>::value, int> = 0>
    void connect_handle(const boost::system::error_code& e) {
        DEBUG(__func__);
        if (e) {
            ERROR("connect error " + e.message());
            return;
        }

        auto self = this->shared_from_this();
        auto req = Http::Request::get(host_, uri_);
        boost::asio::async_write(socket_, boost::asio::buffer(req),
                                 boost::bind(&Connection::write_handle, self,
                                             boost::asio::placeholders::error));
    }

    // ssl only
    template <typename T = SocketT,
              std::enable_if_t<TypeTraits::is_ssl<T>::value, int> = 0>
    void handshake_handle(const boost::system::error_code& e) {
        DEBUG(__func__);
        if (e) {
            ERROR("async_handshake error " + e.message());
            return;
        }

        auto self = this->shared_from_this();
        auto req = Http::Request::get(host_, uri_);
        boost::asio::async_write(socket_, boost::asio::buffer(req),
                                 boost::bind(&Connection::write_handle, self,
                                             boost::asio::placeholders::error));
    }

    void write_handle(const boost::system::error_code& e) {
        DEBUG(__func__);
        if (e) {
            ERROR("async_write error " + e.message());
            return;
        }

        auto self = this->shared_from_this();
        boost::asio::async_read(
            socket_, boost::asio::buffer(buffer_, buffer_.size()),
            boost::bind(&Connection::read_header_handle, self,
                        boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred));
    }

    void read_header_handle(const boost::system::error_code& e,
                            std::size_t bytes_transferred) {
        DEBUG(__func__);
        if (e && bytes_transferred == 0) {
            ERROR("async_read error " + e.message());
            return;
        }

        auto self = this->shared_from_this();
        typename decltype(buffer_)::iterator iter;
        Http::Response::Parser::state st;
        std::tie(st, iter) = parser_.parse(resp_, buffer_.data(),
                                           buffer_.data() + bytes_transferred);
        if (st == Http::Response::Parser::state::bad) {
            ERROR("parse http header error, close socket");
            // close socket
        } else if (st == Http::Response::Parser::state::good) {
            // redirect
            if (resp_.status_code_ >= 300 && resp_.status_code_ <= 307) {
                std::string loc = resp_.getHeader("location");
                DEBUG("redirect to " + loc);
                std::string protocol, host, uri;
                std::tie(protocol, host, uri) = Http::parseURL(std::move(loc));
                startConnection(socket_.get_io_service(), std::move(protocol),
                                std::move(host), std::move(uri),
                                std::move(callback_));
                return;
            }

            // non-text/html
            std::string content_type = resp_.getHeader("content-type");
            if (content_type.empty()) {
                return;
            } else if (content_type.compare(0, sizeof("text/html") - 1, "text/html")) {
                std::string content_length = resp_.getHeader("content-length");
                callback_(content_type + " " + numfmt(content_length));
                return;
            }

            // text/html
            // set charset converter if needed
            // for example:
            // Content-Type: text/html; charset=gb2312
            {
                static const char charset[] = "charset";
                static const char sp[] = " ";
                auto iter =
                    std::search(content_type.begin(), content_type.end(),
                                charset, charset + sizeof(charset) - 1);
                if (iter != content_type.end()) {
                    auto st = iter + sizeof(charset); // include '='
                    auto ed = std::find_first_of(st, content_type.end(), sp, sp + 1);
                    std::string codec(st, ed);
                    trim_left_if(codec, boost::is_any_of("\"\'"));
                    trim_right_if(codec, boost::is_any_of("\"\'"));
                    boost::to_lower(codec);
                    DEBUG("charset=" + codec);
                    if (codec.compare(0, 3, "utf")) { // XXX
                        title_parser_.setConverter(
                            std::make_unique<iconvpp::converter>(
                                "UTF-8", std::move(codec)));
                    }
                }
            }

            // process Transfer-Encoding and Content-Encoding
            {
                std::string encoding = resp_.getHeader("content-encoding");
                if (encoding.empty()) {
                    content_decoder_ =
                        std::make_shared<ContentDecoder>(title_parser_);
                } else if (encoding == "gzip") {
                    content_decoder_ =
                        std::make_shared<GzipDecoder>(title_parser_);
                } else if (encoding == "deflate") {
                    content_decoder_ =
                        std::make_shared<DeflateDecoder>(title_parser_);
                } else {
                    ERROR("un-implement encoding = " + encoding);
                    return;
                }
            }
            chunk_decoder_ = std::make_shared<ChunkDecoder>(content_decoder_);
            if (resp_.getHeader("transfer-encoding") == "chunked") {
                DEBUG("set Chunked Parser");
                chunk_decoder_->setParser(std::make_unique<Http::Chunk::Parser>());
            }

            // title not found
            if (!chunk_decoder_->parse(iter,
                                       buffer_.data() + bytes_transferred)) {
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

    void read_content_handle(const boost::system::error_code& e,
                             std::size_t bytes_transferred) {
        DEBUG(__func__);
        if (e && bytes_transferred == 0) {
            ERROR("read content handle error " + e.message());
            return;
        }

        auto self = this->shared_from_this();
        if (!chunk_decoder_->parse(buffer_.data(),
                                   buffer_.data() + bytes_transferred)) {
            boost::asio::async_read(
                socket_, boost::asio::buffer(buffer_, buffer_.size()),
                boost::bind(&Connection::read_content_handle, self,
                            boost::asio::placeholders::error,
                            boost::asio::placeholders::bytes_transferred));
        }
    }

private:
    boost::asio::io_service& io_service_;

    SocketT socket_;

    boost::asio::ip::tcp::resolver resolver_;
    std::string protocol_, host_, uri_;

    std::function<void(std::string)> callback_;

    std::array<char, 2048> buffer_;
    Http::Response resp_;
    Http::Response::Parser parser_;

    // keep order
    TitleParser title_parser_;
    std::shared_ptr<ContentDecoder> content_decoder_;
    std::shared_ptr<ChunkDecoder> chunk_decoder_;
};


using HTTPConnection = Connection<boost::asio::ip::tcp::socket>;
using HTTPSConnection =
    Connection<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>;
