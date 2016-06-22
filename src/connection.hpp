#pragma once

#include "global.hpp"
#include "decoder.hpp"
#include "dns_cache.hpp"
#include "utils/log.hpp"
#include "utils/http.hpp"
#include "utils/iconv.hpp"
#include "utils/format.hpp"
#include "title_parser.hpp"
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/algorithm/string.hpp>
#include <algorithm>
#include <type_traits>
#include <memory>
#include <array>

namespace TypeTraits {
template <typename T>
struct is_ssl : std::false_type {};

template <>
struct is_ssl<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>
    : std::true_type {};
}

void startConnection(boost::asio::io_service& io_service, std::string schema,
                     std::string host, std::string uri,
                     std::function<void(std::string)> cb,
                     std::size_t redirect_cnt = 0);


template <typename SocketT>
class Connection : public std::enable_shared_from_this<Connection<SocketT>> {
public:
    // ssl version
    template <typename T = SocketT,
              std::enable_if_t<TypeTraits::is_ssl<T>::value, int> = 0>
    Connection(boost::asio::io_service& io_service, std::string schema,
               std::string host, std::string uri,
               std::function<void(std::string)> cb,
               std::size_t redirect_cnt)
        : io_service_(io_service),
          socket_(io_service, Global::getSSLContext().native()),
          resolver_(io_service),
          schema_(std::move(schema)),
          host_(std::move(host)),
          uri_(std::move(uri)),
          callback_(cb),
          title_parser_(std::move(cb)),
          redirect_cnt_(redirect_cnt) {}

    // normal version
    template <typename T = SocketT,
              std::enable_if_t<!TypeTraits::is_ssl<T>::value, int> = 0>
    Connection(boost::asio::io_service& io_service, std::string schema,
               std::string host, std::string uri,
               std::function<void(std::string)> cb,
               std::size_t redirect_cnt)
        : io_service_(io_service),
          socket_(io_service),
          resolver_(io_service),
          schema_(std::move(schema)),
          host_(std::move(host)),
          uri_(std::move(uri)),
          callback_(cb),
          title_parser_(std::move(cb)),
          redirect_cnt_(redirect_cnt) {}

    void start() {
        DEBUG(__func__);
        auto self = this->shared_from_this();
        io_service_.post(boost::bind(&Connection::do_start, self));
    }

private:
    void do_start() {
        DEBUG(__func__);
        auto self = this->shared_from_this();
        // dns lookup cache
        auto opt = Global::getDNSCache().get(schema_, host_);
        if (opt) {
            DEBUG("dns cache hit");
            resolve_handle<SocketT>(boost::system::error_code(), opt.value());
            return;
        }

        boost::asio::ip::tcp::resolver::query query(host_, schema_);
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
            Global::getDNSCache().erase(schema_, host_);
            return;
        }

        // Update dns cache
        const auto entry = *ep_iter;
        std::string serv = entry.service_name(), host = entry.host_name();
        Global::getDNSCache().put(std::move(serv), std::move(host), ep_iter);

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
            Global::getDNSCache().erase(schema_, host_);
            return;
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
                std::string loc = resp_.getHeader("location", false);
                DEBUG("redirect to " + loc);
                redirect(std::move(loc));
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
            // process charset
            // e.g. Content-Type: text/html;charset=gbk
            {
                static const char charset[] = {'c','h','a','r','s','e','t'};
                auto iter = std::search(content_type.begin(), content_type.end(),
                                        std::begin(charset), std::end(charset));
                if (iter != content_type.end()) {
                    iter += sizeof(charset) + 1;
                    auto sp = std::find(iter, content_type.end(), ' ');
                    title_parser_.setCharsetDecoder(
                        std::make_unique<iconvpp::converter>(
                            "UTF-8", boost::trim_copy_if(
                                         std::string(iter, sp), [](char c) {
                                             return c == '\'' || c == '\"';
                                         })));
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

    void redirect(std::string loc) {
        ++redirect_cnt_;
        if (redirect_cnt_ > 3) {
            ERROR("too many redirections");
            return;
        }

        auto self = this->shared_from_this();
        if (loc.front() != '/') {
            std::tie(schema_, host_, uri_) = Http::parseURL(std::move(loc));
            startConnection(socket_.get_io_service(), std::move(schema_),
                            std::move(host_), std::move(uri_),
                            std::move(callback_), redirect_cnt_);
        } else {
            uri_ = std::move(loc);
            auto req = Http::Request::get(host_, uri_);
            boost::asio::async_write(
                socket_, boost::asio::buffer(req),
                boost::bind(&Connection::write_handle, self,
                            boost::asio::placeholders::error));
        }
    }

private:
    boost::asio::io_service& io_service_;

    SocketT socket_;

    boost::asio::ip::tcp::resolver resolver_;
    std::string schema_, host_, uri_;

    std::function<void(std::string)> callback_;

    std::array<char, 2048> buffer_;
    Http::Response resp_;
    Http::Response::Parser parser_;

    // keep order
    TitleParser title_parser_;
    std::shared_ptr<ContentDecoder> content_decoder_;
    std::shared_ptr<ChunkDecoder> chunk_decoder_;

    // policy
    std::size_t redirect_cnt_;
};


using HTTPConnection = Connection<boost::asio::ip::tcp::socket>;
using HTTPSConnection =
    Connection<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>;
