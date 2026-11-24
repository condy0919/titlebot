#include "fetcher.hpp"
#include "common/utils.hpp"
#include "html/title_parser.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <chrono>

namespace tb {
namespace {
namespace asio = boost::asio;
namespace http = boost::beast::http;
using asio::use_awaitable;
using asio::ip::tcp;

bool IsPublic(asio::ip::address_v4 address) {
    auto b = address.to_bytes();
    return b[0] != 0 && b[0] != 10 && b[0] != 127 && b[0] < 224 &&
           !(b[0] == 100 && (b[1] & 192) == 64) && !(b[0] == 169 && b[1] == 254) &&
           !(b[0] == 172 && b[1] >= 16 && b[1] <= 31) &&
           !(b[0] == 192 && (b[1] == 168 || (b[1] == 0 && (b[2] == 0 || b[2] == 2)) ||
                             (b[1] == 88 && b[2] == 99))) &&
           !(b[0] == 198 && (b[1] == 18 || b[1] == 19 || (b[1] == 51 && b[2] == 100))) &&
           !(b[0] == 203 && b[1] == 0 && b[2] == 113);
}

template <class Stream>
asio::awaitable<FetchResult> ReadTitle(Stream& stream, std::string authority, std::string path) {
    http::request<http::empty_body> request(http::verb::get, path, 11);
    request.set(http::field::host, authority);
    request.set(http::field::accept_encoding, "identity");
    co_await http::async_write(stream, request, use_awaitable);
    boost::beast::flat_buffer buffer;
    http::response_parser<http::buffer_body> response;
    response.header_limit(16384);
    response.body_limit(512 * 1024);
    co_await http::async_read_header(stream, buffer, response, use_awaitable);
    if (response.get().result() != http::status::ok) {
        co_return std::nullopt;
    }
    TitleParser parser;
    std::array<char, 4096> block;
    while (!response.is_done()) {
        auto& body = response.get().body();
        body.data = block.data();
        body.size = block.size();
        boost::system::error_code ec;
        co_await http::async_read_some(stream, buffer, response,
                                       asio::redirect_error(use_awaitable, ec));
        if (ec && ec != http::error::need_buffer) {
            co_return std::unexpected(FetchError::kNetwork);
        }
        if (auto title = parser.parse(block.begin(), block.end() - body.size)) {
            auto text = utf8(*title);
            if (text) {
                co_return unquote(text->c_str());
            }
            co_return std::nullopt;
        }
    }
    co_return std::nullopt;
}
} // namespace

asio::awaitable<FetchResult> FetchTitle(PrivMsg msg, asio::ssl::context& tls) {
    auto split = msg.url.find_first_of("/?#");
    auto authority = msg.url.substr(0, split);
    auto colon = authority.find(':');
    auto host = authority.substr(0, colon);
    bool secure = msg.protocol == "https";
    auto port = colon == authority.npos ? (secure ? "443" : "80") : authority.substr(colon + 1);
    if ((!secure && msg.protocol != "http") || host.empty() ||
        host.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") != host.npos ||
        port.empty() || port.find_first_not_of("0123456789") != port.npos) {
        co_return std::unexpected(FetchError::kInvalidUrl);
    }
    std::string path = split == msg.url.npos ? "/" : msg.url.substr(split);
    path = path.substr(0, path.find('#'));
    if (!path.starts_with('/')) {
        path.insert(0, "/");
    }
    auto executor = co_await asio::this_coro::executor;
    auto resolver = std::make_shared<tcp::resolver>(executor);
    auto stream = std::make_shared<asio::ssl::stream<tcp::socket>>(executor, tls);
    asio::steady_timer timer(executor, std::chrono::seconds(10));
    timer.async_wait([resolver, stream](boost::system::error_code ec) {
        if (!ec) {
            resolver->cancel();
            boost::system::error_code ignored;
            stream->next_layer().close(ignored);
        }
    });
    try {
        auto answers = co_await resolver->async_resolve(tcp::v4(), host, port, use_awaitable);
        for (auto& answer : answers) {
            if (!IsPublic(answer.endpoint().address().to_v4())) {
                co_return std::unexpected(FetchError::kBlockedAddress);
            }
        }
        co_await asio::async_connect(stream->next_layer(), answers, use_awaitable);
        if (secure) {
            stream->set_verify_mode(asio::ssl::verify_peer);
            stream->set_verify_callback(asio::ssl::host_name_verification(host));
            if (!SSL_set_tlsext_host_name(stream->native_handle(), host.c_str())) {
                co_return std::unexpected(FetchError::kNetwork);
            }
            co_await stream->async_handshake(asio::ssl::stream_base::client, use_awaitable);
            co_return co_await ReadTitle(*stream, authority, path);
        }
        co_return co_await ReadTitle(stream->next_layer(), authority, path);
    } catch (const boost::system::system_error&) {
        co_return std::unexpected(timer.expiry() <= std::chrono::steady_clock::now()
                                      ? FetchError::kTimeout
                                      : FetchError::kNetwork);
    }
}
} // namespace tb
