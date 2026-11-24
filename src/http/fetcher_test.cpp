#include "fetcher.hpp"

#include <boost/asio.hpp>
#include <gtest/gtest.h>

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
} // namespace
