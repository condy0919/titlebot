#include "fetch_scheduler.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

#include <boost/asio.hpp>

#include "irc/policy.hpp"

namespace {
namespace asio = boost::asio;

class SchedulerTest : public testing::Test {
public:
    void Submit(std::string url, const std::string& target = "#room") {
        scheduler->Submit(
            {
                .who = "alice",
                .channel = target,
                .protocol = "https",
                .url = std::move(url),
            },
            [this, target](const std::string& title) {
                delivered.push_back(target + " " + title);
            });
    }

protected:
    asio::io_context io;
    asio::steady_timer release{io, tb::Clock::time_point::max()};
    std::vector<std::string> started;
    std::vector<std::string> delivered;
    std::shared_ptr<tb::FetchScheduler> scheduler = std::make_shared<tb::FetchScheduler>(
        io.get_executor(), [this](tb::PrivMsg msg) -> asio::awaitable<tb::FetchResult> {
            started.push_back(msg.url);
            boost::system::error_code ignored;
            co_await release.async_wait(asio::redirect_error(asio::use_awaitable, ignored));
            if (msg.url.ends_with("timeout")) {
                co_return std::unexpected(tb::FetchError::kTimeout);
            }
            if (msg.url == "decode") {
                co_return std::unexpected(tb::FetchError::kDecode);
            }
            if (msg.url == "missing") {
                co_return std::nullopt;
            }
            co_return msg.url;
        });
};

TEST_F(SchedulerTest, SharesDownloadsAndSkipsHostAtCapacity) {
    Submit("slow/a", "#first");
    Submit("slow/a", "#second");
    Submit("SLOW.:443/b");
    Submit("slow/c");
    Submit("other/timeout");
    io.poll();
    EXPECT_EQ(started, (std::vector<std::string>{"slow/a", "SLOW.:443/b", "other/timeout"}));
    // Each of the three active downloads contributes at least this interval to elapsed.
    constexpr auto interval = std::chrono::milliseconds(7);
    asio::steady_timer(io, interval).wait();
    EXPECT_EQ(release.cancel(), 3u);
    io.poll();
    EXPECT_EQ(started.back(), "slow/c");
    EXPECT_EQ(release.cancel(), 1u);
    io.run();
    EXPECT_EQ(scheduler->stats().completed, 4u);
    EXPECT_EQ(scheduler->stats().failures.at(tb::FetchError::kTimeout), 1u);
    EXPECT_EQ(scheduler->stats().merged, 1u);
    EXPECT_GE(scheduler->stats().elapsed, 3 * interval);
    std::ranges::sort(delivered);
    EXPECT_EQ(delivered, (std::vector<std::string>{"#first slow/a", "#room SLOW.:443/b",
                                                   "#room slow/c", "#second slow/a"}));
}

TEST_F(SchedulerTest, SeparatesCompletionOutcomes) {
    for (auto url : {"ok", "missing", "decode", "timeout"}) {
        Submit(url);
    }
    io.poll();
    release.cancel();
    io.run();
    const auto& stats = scheduler->stats();
    EXPECT_EQ(stats.completed, 4u);
    EXPECT_EQ(stats.titles, 1u);
    EXPECT_EQ(stats.no_title, 1u);
    EXPECT_EQ(stats.failures.at(tb::FetchError::kDecode), 1u);
    EXPECT_EQ(stats.failures.at(tb::FetchError::kTimeout), 1u);
}

TEST_F(SchedulerTest, CompletesImmediateFetchesFromTheExecutor) {
    scheduler = std::make_shared<tb::FetchScheduler>(
        io.get_executor(), [](tb::PrivMsg msg) -> asio::awaitable<tb::FetchResult> {
            if (msg.url == "error") {
                throw std::runtime_error("fetch failed");
            }
            co_return msg.url;
        });
    asio::post(io, [this] {
        Submit("ok");
        Submit("error");
    });
    io.run();
    EXPECT_EQ(scheduler->stats().completed, 2u);
    EXPECT_EQ(scheduler->stats().failures.at(tb::FetchError::kNetwork), 1u);
    EXPECT_EQ(delivered, (std::vector<std::string>{"#room ok"}));
}

TEST_F(SchedulerTest, BoundsActiveAndWaitingWork) {
    for (unsigned i = 0; i < 100; ++i) {
        Submit("host" + std::to_string(i) + "/");
    }
    io.poll();
    EXPECT_EQ(started.size(), 8u);
    EXPECT_EQ(scheduler->stats().peak_queue, 64u);
    EXPECT_EQ(scheduler->stats().queue_drops, 28u);
    scheduler->RecordReplyDrop();
    EXPECT_EQ(scheduler->stats().reply_drops, 1u);
    EXPECT_EQ(scheduler->stats().queue_drops, 28u);
    while (release.cancel()) {
        io.poll();
    }
    EXPECT_EQ(delivered.size(), 72u);
}

TEST_F(SchedulerTest, EquivalentIrcTargetsShareReplyQuota) {
    tb::IrcReplyPolicy limiter;
    constexpr tb::Clock::time_point now{};
    EXPECT_TRUE(limiter.Allow(R"(#A[B]\C^)", now));
    EXPECT_TRUE(limiter.Allow("#a{b}|c~", now));
    EXPECT_FALSE(limiter.Allow(R"(#a[b]\c^)", now));
    EXPECT_TRUE(limiter.Allow("#other", now));
}

TEST_F(SchedulerTest, BoundsSubscribersForOneDownload) {
    constexpr tb::Clock::time_point now{};
    tb::IrcReplyPolicy limiter;
    unsigned replies = 0;
    for (unsigned i = 0; i < 100; ++i) {
        scheduler->Submit(
            {
                .who = "a",
                .channel = "#one",
                .protocol = "https",
                .url = "same/",
            },
            [&](const std::string&) { replies += limiter.Allow("#one", now); });
    }
    io.poll();
    EXPECT_EQ(started.size(), 1u);
    release.cancel();
    io.run();
    EXPECT_EQ(replies, 2u);
    EXPECT_EQ(scheduler->stats().queue_drops, 28u);
    EXPECT_EQ(scheduler->stats().merged, 71u);
    // Each channel has its own quota. The original channel can send again when its window expires.
    EXPECT_TRUE(limiter.Allow("#two", now));
    EXPECT_FALSE(limiter.Allow("#ONE", now + std::chrono::milliseconds(1999)));
    EXPECT_TRUE(limiter.Allow("#one", now + std::chrono::seconds(2)));
}
TEST_F(SchedulerTest, ReusesRecentTitlesAndFailures) {
    Submit("found");
    Submit("timeout");
    io.poll();
    release.cancel();
    io.run();
    Submit("found", "#other");
    Submit("timeout");
    EXPECT_EQ(started.size(), 2u);
    EXPECT_EQ(scheduler->stats().cache_hits, 2u);
    EXPECT_EQ(delivered, (std::vector<std::string>{"#room found", "#other found"}));
    EXPECT_EQ(scheduler->stats().completed, 2u);
}
TEST_F(SchedulerTest, AppliesCustomReplyQuotas) {
    tb::IrcReplyPolicy limiter({.per_target = 1, .targets = 1});
    tb::Clock::time_point start{};
    EXPECT_TRUE(limiter.Allow("#one", start));
    EXPECT_FALSE(limiter.Allow("#one", start));
    EXPECT_FALSE(limiter.Allow("#two", start));
    EXPECT_TRUE(limiter.Allow("#two", start + std::chrono::seconds(2)));
}
TEST_F(SchedulerTest, EvictsLeastRecentlyUsedTitlesAndSkipsDisabledNegativeCaching) {
    scheduler = std::make_shared<tb::FetchScheduler>(
        io.get_executor(),
        [this](tb::PrivMsg msg) -> asio::awaitable<tb::FetchResult> {
            started.push_back(msg.url);
            if (msg.url == "timeout") {
                co_return std::unexpected(tb::FetchError::kTimeout);
            }
            co_return msg.url;
        },
        tb::SchedulerLimits{.cache_entries = 2, .negative_ttl = std::chrono::milliseconds(0)});
    for (auto url : {"a", "b", "a", "c", "b", "timeout", "timeout"}) {
        io.restart();
        Submit(url);
        io.run();
    }
    EXPECT_EQ(started, (std::vector<std::string>{"a", "b", "c", "b", "timeout", "timeout"}));
}
} // namespace
