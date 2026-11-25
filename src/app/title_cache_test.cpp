#include "title_cache.hpp"

#include <gtest/gtest.h>
namespace {
class TitleCache : public testing::Test {};
TEST_F(TitleCache, RemovesExpiredFailuresBeforeLiveLeastRecentTitle) {
    using namespace std::chrono_literals;
    tb::TitleCache cache({.cache_entries = 2, .title_ttl = 1min, .negative_ttl = 1s});
    const std::chrono::steady_clock::time_point start{};
    cache.Store("old", std::string("old title"), start);
    cache.Store("failed", std::unexpected(tb::FetchError::kTimeout), start);
    ASSERT_TRUE(cache.Find("failed", start));
    cache.Store("new", std::string("new title"), start + 1s);
    EXPECT_FALSE(cache.Find("failed", start + 1s));
    EXPECT_TRUE(cache.Find("old", start + 1s));
    EXPECT_TRUE(cache.Find("new", start + 1s));
}
} // namespace
