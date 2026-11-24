#include "fetcher.hpp"
#include "fetcher_internal.hpp"
#include <boost/asio/ssl.hpp>

#include <boost/asio.hpp>
#include <gtest/gtest.h>
#include <zlib.h>

namespace {
tb::FetchResult Fetch(std::string protocol, std::string url) {
    boost::asio::io_context io;
    boost::asio::ssl::context tls(boost::asio::ssl::context::tls_client);
    auto result = boost::asio::co_spawn(
        io, tb::FetchTitle({"alice", "#test", std::move(protocol), std::move(url)}, tls),
        boost::asio::use_future);
    io.run();
    return result.get();
}

TEST(Fetcher, RejectsUnsupportedScheme) {
    EXPECT_EQ(Fetch("file", "example.org"), std::unexpected(tb::FetchError::kInvalidUrl));
}

TEST(Fetcher, RejectsNonPublicDestinations) {
    // 使用 IP 字面量，验证连接前的地址检查，无需外部 DNS 服务。
    for (auto authority : {"127.0.0.1", "169.254.169.254", "10.0.0.1", "192.168.1.1"}) {
        EXPECT_EQ(Fetch("https", authority), std::unexpected(tb::FetchError::kBlockedAddress));
    }
}

namespace asio = boost::asio;
using asio::ip::tcp;
using std::chrono_literals::operator""s;
using std::chrono_literals::operator""ms;

class FetcherLoopback : public testing::Test {
protected:
    asio::io_context io;
    tcp::acceptor listener{io, {asio::ip::address_v4::loopback(), 0}};
    asio::ssl::context client_tls{asio::ssl::context::tls_client};
    asio::ssl::context server_tls{asio::ssl::context::tls_server};
    std::vector<std::string> requests;
    tb::detail::FetchPolicy policy{
        [this](tcp::endpoint endpoint) { return endpoint == listener.local_endpoint(); }, 2s, 1s};
    std::string authority = "127.0.0.1:" + std::to_string(listener.local_endpoint().port());

    template <typename Stream>
    asio::awaitable<void> Respond(Stream& stream, std::string response) {
        std::string request;
        co_await asio::async_read_until(stream, asio::dynamic_buffer(request), "\r\n\r\n",
                                        asio::use_awaitable);
        requests.push_back(request.substr(0, request.find("\r\n")));
        if (response.empty()) {
            // 等待客户端因总时限关闭连接，然后继续处理下一条请求。
            char byte;
            boost::system::error_code ignored;
            co_await stream.async_read_some(asio::buffer(&byte, 1),
                                            asio::redirect_error(asio::use_awaitable, ignored));
        } else {
            co_await asio::async_write(stream, asio::buffer(response), asio::use_awaitable);
        }
    }

    asio::awaitable<void> Serve(std::vector<std::string> responses, bool secure) {
        for (auto& response : responses) {
            auto socket = co_await listener.async_accept(asio::use_awaitable);
            if (secure) {
                asio::ssl::stream<tcp::socket> stream(std::move(socket), server_tls);
                boost::system::error_code ec;
                co_await stream.async_handshake(asio::ssl::stream_base::server,
                                                asio::redirect_error(asio::use_awaitable, ec));
                if (!ec) {
                    co_await Respond(stream, response);
                }
            } else {
                co_await Respond(socket, response);
            }
        }
    }

    tb::FetchResult Run(std::vector<std::string> responses, std::string scheme = "http",
                        std::string host = "") {
        io.restart();
        auto server =
            asio::co_spawn(io, Serve(std::move(responses), scheme == "https"), asio::use_future);
        auto client =
            asio::co_spawn(io,
                           tb::detail::FetchWithPolicy(
                               {"", "", scheme, (host.empty() ? authority : host) + "/a/start"},
                               client_tls, policy),
                           asio::use_future);
        io.run_for(3s);
        if (client.wait_for(0s) != std::future_status::ready ||
            server.wait_for(0s) != std::future_status::ready) {
            throw std::runtime_error("loopback test timed out");
        }
        server.get();
        return client.get();
    }

    const std::string ok = "HTTP/1.1 200 OK\r\nContent-Length: 17\r\n\r\n<title>OK</title>";
    std::string Redirect(std::string target) {
        return "HTTP/1.1 302 Found\r\nLocation: " + target + "\r\nContent-Length: 0\r\n\r\n";
    }
};

TEST_F(FetcherLoopback, ResolvesRelativeReferencesAndRechecksDestinations) {
    // 查询和片段与当前 URL 合并，传给服务器的请求中应省略片段。
    EXPECT_EQ(Run({Redirect("../next?q=1#fragment"), Redirect("?q=2"), ok}),
              tb::FetchResult(std::string("OK")));
    EXPECT_EQ(requests, (std::vector<std::string>{"GET /a/start HTTP/1.1", "GET /next?q=1 HTTP/1.1",
                                                  "GET /next?q=2 HTTP/1.1"}));
    EXPECT_EQ(Run({Redirect("//127.0.0.2/")}), std::unexpected(tb::FetchError::kBlockedAddress));
    EXPECT_EQ(Run({Redirect("file:///tmp/title")}), std::unexpected(tb::FetchError::kInvalidUrl));
}

TEST_F(FetcherLoopback, BoundsRedirectsAndResponseSizes) {
    EXPECT_EQ(Run(std::vector<std::string>(6, Redirect("/loop"))),
              std::unexpected(tb::FetchError::kRedirectLimit));
    EXPECT_EQ(Run({"HTTP/1.1 200 OK\r\nContent-Length: 524289\r\n\r\n"}),
              std::unexpected(tb::FetchError::kResponseLimit));
    EXPECT_EQ(Run({"HTTP/1.1 200 OK\r\nX-Large: " + std::string(17000, 'x') + "\r\n\r\n"}),
              std::unexpected(tb::FetchError::kResponseLimit));
}

TEST_F(FetcherLoopback, TimesOutAndCanFetchAgain) {
    policy.total_timeout = 100ms;
    EXPECT_EQ(Run({""}), std::unexpected(tb::FetchError::kTimeout));
    EXPECT_EQ(Run({ok}), tb::FetchResult(std::string("OK")));
}

TEST_F(FetcherLoopback, VerifiesTrustAndHostForEachTlsConnection) {
    const std::string fixture =
        std::string(TITLEBOT_PROJ_ROOT) + "/tests/fixtures/localhost-test.pem";
    server_tls.use_certificate_chain_file(fixture);
    server_tls.use_private_key_file(fixture, asio::ssl::context::pem);
    auto localhost = "localhost:" + std::to_string(listener.local_endpoint().port());
    // 同一张证书依次覆盖不受信任、受信但主机名不符、完全匹配三个场景。
    EXPECT_EQ(Run({ok}, "https", localhost), std::unexpected(tb::FetchError::kNetwork));
    client_tls.load_verify_file(fixture);
    EXPECT_EQ(Run({ok}, "https"), std::unexpected(tb::FetchError::kNetwork));
    EXPECT_EQ(Run({ok}, "https", localhost), tb::FetchResult(std::string("OK")));
}
std::string HtmlResponse(const std::string& body, const std::string& headers) {
    return "HTTP/1.1 200 OK\r\n" + headers + "Content-Length: " + std::to_string(body.size()) +
           "\r\n\r\n" + body;
}

std::string Deflate(const std::string& html) {
    uLongf size = compressBound(html.size());
    std::string body(size, '\0');
    if (compress(reinterpret_cast<Bytef*>(body.data()), &size,
                 reinterpret_cast<const Bytef*>(html.data()), html.size()) != Z_OK) {
        throw std::runtime_error("fixture compression failed");
    }
    body.resize(size);
    return body;
}

TEST_F(FetcherLoopback, DecodesDeclaredCharsetEntitiesAndWhitespace) {
    std::string html = "<title>  Caf\xe9\t&amp;\n &#x1F642;  </title>";
    auto response = HtmlResponse(
        Deflate(html),
        "Content-Type: text/html; charset=\"iso-8859-1\"\r\nContent-Encoding: deflate\r\n");
    EXPECT_EQ(Run({response}), tb::FetchResult(std::string("Café & 🙂")));
    EXPECT_EQ(Run({HtmlResponse(html, "Content-Type: text/plain\r\n")}),
              tb::FetchResult(std::nullopt));
}

TEST_F(FetcherLoopback, DistinguishesMissingTitleFromInvalidOrOversizedContent) {
    const std::string coding = "Content-Encoding: deflate\r\n";
    auto body = Deflate("<html>no title</html>");
    EXPECT_EQ(Run({HtmlResponse(body, coding)}), tb::FetchResult(std::nullopt));
    // 截除校验尾部或传入损坏数据，应报告解码失败。
    body.pop_back();
    EXPECT_EQ(Run({HtmlResponse(body, coding)}), std::unexpected(tb::FetchError::kDecode));
    EXPECT_EQ(Run({HtmlResponse("broken", coding)}), std::unexpected(tb::FetchError::kDecode));
    EXPECT_EQ(Run({HtmlResponse(Deflate(std::string(524289, 'x')), coding)}),
              std::unexpected(tb::FetchError::kResponseLimit));
}

TEST_F(FetcherLoopback, ReturnsCompleteTitleBeforeBodyEnds) {
    // 服务端只发前缀；若客户端等待剩余正文，则会收到截断错误。
    EXPECT_EQ(Run({"HTTP/1.1 200 OK\r\nContent-Length: 8192\r\n\r\n<title>early</title>"}),
              tb::FetchResult(std::string("early")));
}
} // namespace
