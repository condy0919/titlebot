#include "common/utils.hpp"
#include "decoder.hpp"
#include "fetcher_internal.hpp"
#include "html/title_parser.hpp"
#include <boost/algorithm/string/trim.hpp>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/url.hpp>
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

FetchResult FormatTitle(const std::string& title, const std::string& charset) {
    auto text = utf8(title, charset);
    if (!text) {
        return std::unexpected(FetchError::kDecode);
    }
    auto decoded = unquote(text->c_str());
    std::string normalized;
    bool space = false;
    for (char ch : decoded) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            space = !normalized.empty();
        } else {
            if (space) {
                normalized += ' ';
                space = false;
            }
            normalized += ch;
        }
    }
    return normalized;
}

template <class Stream>
asio::awaitable<FetchResult> ReadTitle(Stream& stream, std::string authority, std::string path,
                                       std::string& location) {
    http::request<http::empty_body> request(http::verb::get, path, 11);
    request.set(http::field::host, authority);
    request.set(http::field::accept_encoding, "gzip, deflate, identity");
    co_await http::async_write(stream, request, use_awaitable);
    boost::beast::flat_buffer buffer;
    http::response_parser<http::buffer_body> response;
    response.header_limit(16384);
    response.body_limit(512 * 1024);
    co_await http::async_read_header(stream, buffer, response, use_awaitable);
    switch (response.get().result_int()) {
    case 301:
    case 302:
    case 303:
    case 307:
    case 308:
        location = response.get()[http::field::location];
        if (location.empty()) {
            co_return std::unexpected(FetchError::kInvalidUrl);
        }
        co_return std::nullopt;
    }
    if (response.get().result() != http::status::ok) {
        co_return std::nullopt;
    }
    auto type = response.get()[http::field::content_type];
    auto separator = type.find(';');
    auto mime = boost::algorithm::trim_copy(std::string(type.substr(0, separator)));
    if (!mime.empty() && !boost::beast::iequals(mime, "text/html") &&
        !boost::beast::iequals(mime, "application/xhtml+xml")) {
        co_return std::nullopt;
    }
    std::string charset;
    if (separator != type.npos) {
        for (const auto& [key, value] : http::param_list(type.substr(separator))) {
            if (boost::beast::iequals(key, "charset")) {
                charset = value;
                break;
            }
        }
    }
    auto coding = response.get()[http::field::content_encoding];
    ContentEncoding encoding = ContentEncoding::kIdentity;
    if (boost::beast::iequals(coding, "gzip")) {
        encoding = ContentEncoding::kGzip;
    } else if (boost::beast::iequals(coding, "deflate")) {
        encoding = ContentEncoding::kDeflate;
    } else if (!coding.empty() && !boost::beast::iequals(coding, "identity")) {
        co_return std::unexpected(FetchError::kDecode);
    }
    ContentDecoder decoder(encoding);
    TitleParser parser;
    std::array<char, 4096> block;
    do {
        auto& body = response.get().body();
        body.data = block.data();
        body.size = block.size();
        boost::system::error_code ec;
        if (!response.is_done()) {
            co_await http::async_read_some(stream, buffer, response,
                                           asio::redirect_error(use_awaitable, ec));
        }
        if (ec && ec != http::error::need_buffer) {
            throw boost::system::system_error(ec);
        }
        auto title =
            decoder.Consume(parser, {block.data(), block.size() - body.size}, response.is_done());
        if (!title) {
            co_return std::unexpected(title.error() == DecodeError::kLimit
                                          ? FetchError::kResponseLimit
                                          : FetchError::kDecode);
        }
        if (*title) {
            co_return FormatTitle(**title, charset);
        }
    } while (!response.is_done());
    co_return std::nullopt;
}
using Clock = std::chrono::steady_clock;
asio::awaitable<FetchResult> FetchHop(const boost::urls::url& url, asio::ssl::context& tls,
                                      const detail::FetchPolicy& policy, Clock::time_point deadline,
                                      std::string& location) {
    bool secure = url.scheme_id() == boost::urls::scheme::https;
    if ((!secure && url.scheme_id() != boost::urls::scheme::http) || url.has_userinfo() ||
        url.host().empty() || (url.has_port() && url.port_number() == 0) ||
        url.encoded_host().find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") !=
            std::string_view::npos) {
        co_return std::unexpected(FetchError::kInvalidUrl);
    }
    auto host = url.host();
    std::string port = url.has_port() ? std::string(url.port()) : (secure ? "443" : "80");
    std::string authority(url.encoded_authority());
    std::string path(url.encoded_target());
    if (!path.starts_with('/')) {
        path.insert(0, "/");
    }
    auto executor = co_await asio::this_coro::executor;
    auto resolver = std::make_shared<tcp::resolver>(executor);
    auto stream = std::make_shared<asio::ssl::stream<tcp::socket>>(executor, tls);
    auto expires = std::min(deadline, Clock::now() + policy.connect_timeout);
    asio::steady_timer timer(executor, expires);
    auto cancel = [resolver, stream](boost::system::error_code ec) {
        if (!ec) {
            resolver->cancel();
            boost::system::error_code ignored;
            stream->next_layer().close(ignored);
        }
    };
    timer.async_wait(cancel);
    try {
        auto answers = co_await resolver->async_resolve(tcp::v4(), host, port, use_awaitable);
        for (auto& answer : answers) {
            if (!policy.allow(answer.endpoint())) {
                co_return std::unexpected(FetchError::kBlockedAddress);
            }
        }
        boost::system::error_code ec = asio::error::host_unreachable;
        for (const auto& answer : answers) {
            if (Clock::now() >= expires) {
                co_return std::unexpected(FetchError::kTimeout);
            }
            stream->next_layer().close();
            co_await stream->next_layer().async_connect(answer.endpoint(),
                                                        asio::redirect_error(use_awaitable, ec));
            if (!ec) {
                break;
            }
        }
        if (ec) {
            throw boost::system::system_error(ec);
        }
        if (secure) {
            stream->set_verify_mode(asio::ssl::verify_peer);
            stream->set_verify_callback(asio::ssl::host_name_verification(host));
            if (!SSL_set_tlsext_host_name(stream->native_handle(), host.c_str())) {
                co_return std::unexpected(FetchError::kNetwork);
            }
            co_await stream->async_handshake(asio::ssl::stream_base::client, use_awaitable);
        }
        if (Clock::now() >= expires) {
            co_return std::unexpected(FetchError::kTimeout);
        }
        timer.expires_at(deadline);
        timer.async_wait(cancel);
        if (secure) {
            co_return co_await ReadTitle(*stream, authority, path, location);
        }
        co_return co_await ReadTitle(stream->next_layer(), authority, path, location);
    } catch (const boost::system::system_error& error) {
        if (Clock::now() >= timer.expiry()) {
            co_return std::unexpected(FetchError::kTimeout);
        }
        if (error.code() == http::error::header_limit || error.code() == http::error::body_limit) {
            co_return std::unexpected(FetchError::kResponseLimit);
        }
        co_return std::unexpected(FetchError::kNetwork);
    }
}
} // namespace

asio::awaitable<FetchResult> detail::FetchWithPolicy(PrivMsg msg, asio::ssl::context& tls,
                                                     FetchPolicy policy) {
    // parse_uri 返回视图，解析所用的字符串必须覆盖复制过程。
    auto deadline = Clock::now() + policy.total_timeout;
    boost::urls::url current;
    const std::string original = msg.protocol + "://" + msg.url;
    auto parsed = boost::urls::parse_uri(original);
    if (!parsed) {
        co_return std::unexpected(FetchError::kInvalidUrl);
    }
    current = *parsed;
    for (unsigned redirects = 0;; ++redirects) {
        std::string location;
        auto result = co_await FetchHop(current, tls, policy, deadline, location);
        if (!result || location.empty()) {
            co_return result;
        }
        if (redirects == 5) {
            co_return std::unexpected(FetchError::kRedirectLimit);
        }
        auto reference = boost::urls::parse_uri_reference(location);
        if (!reference || !current.resolve(*reference)) {
            co_return std::unexpected(FetchError::kInvalidUrl);
        }
    }
}

asio::awaitable<FetchResult> FetchTitle(PrivMsg msg, asio::ssl::context& tls) {
    detail::FetchPolicy policy{[](tcp::endpoint ep) { return IsPublic(ep.address().to_v4()); },
                               std::chrono::seconds(10), std::chrono::seconds(3)};
    return detail::FetchWithPolicy(std::move(msg), tls, std::move(policy));
}
} // namespace tb
