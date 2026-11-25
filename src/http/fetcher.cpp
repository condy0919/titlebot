#include "fetcher.hpp"

#include <algorithm>
#include <array>
#include <chrono>

#include <boost/algorithm/string/trim.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core/string.hpp>
#include <boost/beast/http.hpp>
#include <boost/url.hpp>

#include "common/deadline.hpp"
#include "common/utils.hpp"
#include "decoder.hpp"
#include "fetcher_internal.hpp"
#include "html/title_parser.hpp"

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

FetchResult FormatTitle(const std::string& title, const std::string& charset,
                        const TitleParser& parser) {
    std::optional<std::string> text;
    if (parser.unicode_bom()) {
        text = convert_to_utf8(title, "UTF-8");
    } else {
        if (!charset.empty()) {
            text = convert_to_utf8(title, charset);
        }
        auto meta = parser.DeclaredCharset();
        if (!text && !meta.empty() && !boost::beast::iequals(meta, charset)) {
            text = convert_to_utf8(title, meta);
        }
        if (!text) {
            text = convert_to_utf8(title, {}, parser.sample());
        }
    }
    if (!text) {
        return std::unexpected(FetchError::kDecode);
    }
    auto decoded = decode_html_entities(*text);
    std::string normalized;
    bool space = false;
    for (char ch : decoded) {
        if (static_cast<unsigned char>(ch) <= 32 || ch == 127) {
            space = !normalized.empty();
        } else {
            if (space) {
                normalized += ' ';
                space = false;
            }
            normalized += ch;
        }
    }
    if (normalized.empty()) {
        return std::nullopt;
    }
    return normalized;
}

using ResponseParser = http::response_parser<http::buffer_body>;
constexpr std::size_t kBodyLimit = 512 * 1024;

// Keep unconsumed bytes across parser steps and informational responses. For example,
// a read containing both a 103 header and a 200 header leaves the latter for the next parser.
template <class Stream>
asio::awaitable<void> ReadResponsePart(Stream& stream, std::string& pending,
                                       ResponseParser& parser) {
    auto buffer = asio::dynamic_buffer(pending, kBodyLimit);
    boost::system::error_code ec;
    for (;;) {
        if (buffer.size()) {
            const auto consumed = parser.put(buffer.data(0, buffer.size()), ec);
            buffer.consume(consumed);
            if (!ec || ec == http::error::need_buffer) {
                co_return;
            }
            if (ec != http::error::need_more) {
                throw boost::system::system_error(ec);
            }
        }

        const auto offset = buffer.size();
        const auto space = std::min<std::size_t>(4096, buffer.max_size() - offset);
        if (!space) {
            throw boost::system::system_error(http::error::buffer_overflow);
        }
        buffer.grow(space);
        const auto count = co_await stream.async_read_some(buffer.data(offset, space),
                                                           asio::redirect_error(use_awaitable, ec));
        buffer.shrink(space - count);
        if (ec == asio::error::eof && parser.got_some()) {
            parser.put_eof(ec);
            if (!ec) {
                co_return;
            }
        }
        if (ec) {
            throw boost::system::system_error(ec);
        }
    }
}

template <class Stream>
asio::awaitable<FetchResult> ReadTitle(Stream& stream, std::string authority, std::string path,
                                       std::string& location) {
    http::request<http::empty_body> request(http::verb::get, path, 11);
    request.set(http::field::host, authority);
    request.set(http::field::user_agent, "titlebot/0.1");
    request.set(http::field::accept, "text/html, application/xhtml+xml;q=0.9, */*;q=0.1");
    request.keep_alive(false);
    request.set(http::field::accept_encoding, "gzip, deflate, identity");
    co_await http::async_write(stream, request, use_awaitable);
    std::string pending;
    std::optional<ResponseParser> parser_storage;
    for (;;) {
        auto& parser = parser_storage.emplace();
        parser.header_limit(16384);
        parser.body_limit(boost::none);
        while (!parser.is_header_done()) {
            co_await ReadResponsePart(stream, pending, parser);
        }
        auto status = parser.get().result_int();
        if (status == 101) {
            co_return std::nullopt;
        }
        if (status < 100 || status >= 200) {
            break;
        }
    }
    auto& response = *parser_storage;
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
    if (boost::beast::iequals(coding, "gzip") || boost::beast::iequals(coding, "x-gzip")) {
        encoding = ContentEncoding::kGzip;
    } else if (boost::beast::iequals(coding, "deflate")) {
        encoding = ContentEncoding::kDeflate;
    } else if (!coding.empty() && !boost::beast::iequals(coding, "identity")) {
        co_return std::unexpected(FetchError::kDecode);
    }
    ContentDecoder decoder(encoding);
    TitleParser parser;
    std::array<char, 4096> block;
    std::size_t remaining = kBodyLimit;
    do {
        auto& body = response.get().body();
        body.data = block.data();
        const auto capacity = std::min(block.size(), remaining + 1);
        body.size = capacity;
        if (!response.is_done()) {
            co_await ReadResponsePart(stream, pending, response);
        }
        const auto count = capacity - body.size;
        const auto used = std::min(count, remaining);
        remaining -= used;
        auto title = decoder.Consume(parser, {block.data(), used}, response.is_done() && used == count);
        if (!title) {
            co_return std::unexpected(title.error() == DecodeError::kLimit
                                          ? FetchError::kResponseLimit
                                          : FetchError::kDecode);
        }
        if (*title) {
            co_return FormatTitle(**title, charset, parser);
        }
        if (used < count) {
            co_return std::unexpected(FetchError::kResponseLimit);
        }
    } while (!response.is_done());
    co_return std::nullopt;
}

using Clock = std::chrono::steady_clock;
asio::awaitable<void> CancelOnTimeout(asio::awaitable<bool> expired,
                                      std::shared_ptr<tcp::resolver> resolver,
                                      std::shared_ptr<asio::ssl::stream<tcp::socket>> stream) {
    if (co_await std::move(expired)) {
        resolver->cancel();
        boost::system::error_code ignored;
        stream->next_layer().close(ignored);
    }
}

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
    Deadline connect_timeout(executor, expires);
    asio::co_spawn(executor, CancelOnTimeout(connect_timeout.Wait(), resolver, stream),
                   asio::detached);
    try {
        auto answers = co_await resolver->async_resolve(host, port, use_awaitable);
        for (auto& answer : answers) {
            if (!policy.allow(answer.endpoint())) {
                co_return std::unexpected(FetchError::kBlockedAddress);
            }
        }
        boost::system::error_code ec = asio::error::host_unreachable;
        auto remaining_endpoints = answers.size();
        for (const auto& answer : answers) {
            if (Clock::now() >= expires) {
                co_return std::unexpected(FetchError::kTimeout);
            }
            stream->next_layer().close();
            const auto now = Clock::now();
            Deadline attempt(executor, now + (expires - now) / static_cast<long>(remaining_endpoints--));
            asio::co_spawn(executor, [stream, wait = attempt.Wait()]() mutable -> asio::awaitable<void> {
                if (co_await std::move(wait)) {
                    boost::system::error_code ignored;
                    stream->next_layer().cancel(ignored);
                }
            }, asio::detached);
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
            if (url.host_type() == boost::urls::host_type::name &&
                !SSL_set_tlsext_host_name(stream->native_handle(), host.c_str())) {
                co_return std::unexpected(FetchError::kNetwork);
            }
            co_await stream->async_handshake(asio::ssl::stream_base::client, use_awaitable);
        }
        if (Clock::now() >= expires) {
            co_return std::unexpected(FetchError::kTimeout);
        }
        connect_timeout.Cancel();
        expires = deadline;
        Deadline read_timeout(executor, expires);
        asio::co_spawn(executor, CancelOnTimeout(read_timeout.Wait(), resolver, stream),
                       asio::detached);
        if (secure) {
            co_return co_await ReadTitle(*stream, authority, path, location);
        }
        co_return co_await ReadTitle(stream->next_layer(), authority, path, location);
    } catch (const boost::system::system_error& error) {
        if (Clock::now() >= expires) {
            co_return std::unexpected(FetchError::kTimeout);
        }
        if (error.code() == http::error::header_limit || error.code() == http::error::body_limit ||
            error.code() == http::error::buffer_overflow) {
            co_return std::unexpected(FetchError::kResponseLimit);
        }
        co_return std::unexpected(FetchError::kNetwork);
    }
}
} // namespace

asio::awaitable<FetchResult> detail::FetchWithPolicy(PrivMsg msg, asio::ssl::context& tls,
                                                     FetchPolicy policy) {
    // parse_uri returns a view. Keep the source string alive until the URL has been copied.
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
    detail::FetchPolicy policy{[](tcp::endpoint ep) {
                                   auto addr = ep.address();
                                   if (addr.is_v4()) {
                                       return IsPublic(addr.to_v4());
                                   }
                                   auto b = addr.to_v6().to_bytes();
                                   // Allow only global unicast. Exclude protocol assignments,
                                   // documentation and IPv4 tunnel prefixes.
                                   return (b[0] & 0xe0) == 0x20 &&
                                          !(b[0] == 0x20 && b[1] == 1 &&
                                            (b[2] < 2 || (b[2] == 0xd && b[3] == 0xb8))) &&
                                          !(b[0] == 0x20 && b[1] == 2) &&
                                          !(b[0] == 0x3f && b[1] == 0xff && (b[2] & 0xf0) == 0);
                               },
                               std::chrono::seconds(10), std::chrono::seconds(3)};
    return detail::FetchWithPolicy(std::move(msg), tls, std::move(policy));
}
} // namespace tb
