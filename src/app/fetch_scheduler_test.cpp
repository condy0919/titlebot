#include "fetch_scheduler.hpp"
#include <boost/asio.hpp>
#include <gtest/gtest.h>

namespace {
namespace asio = boost::asio;

class SchedulerTest : public testing::Test {
protected:
    asio::io_context io;
    asio::steady_timer release{io, tb::Clock::time_point::max()};
    tb::Clock::time_point now{};
    std::vector<std::string> started;
    std::vector<std::string> delivered;
    std::shared_ptr<tb::FetchScheduler> scheduler = std::make_shared<tb::FetchScheduler>(
        io.get_executor(),
        [this](tb::PrivMsg msg) -> asio::awaitable<tb::FetchResult> {
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
        },
        [this] { return now; });

    void Submit(std::string url, std::string target = "#room") {
        scheduler->Submit({"alice", target, "https", url},
                          [this, target](const std::string& title) {
                              delivered.push_back(target + " " + title);
                          });
    }
};

TEST_F(SchedulerTest, SharesDownloadsAndSkipsHostAtCapacity) {
    Submit("slow/a", "#first");
    Submit("slow/a", "#second");
    Submit("SLOW.:443/b");
    Submit("slow/c");
    Submit("other/timeout");
    io.poll();
    EXPECT_EQ(started, (std::vector<std::string>{"slow/a", "SLOW.:443/b", "other/timeout"}));
    // All three downloads span the same simulated interval. The queued job needs a free slot on the
    // slow host.
    now += std::chrono::milliseconds(7);
    EXPECT_EQ(release.cancel(), 3u);
    io.poll();
    EXPECT_EQ(started.back(), "slow/c");
    EXPECT_EQ(release.cancel(), 1u);
    io.run();
    EXPECT_EQ(scheduler->stats().completed, 4u);
    EXPECT_EQ(scheduler->stats().failures.at(tb::FetchError::kTimeout), 1u);
    EXPECT_EQ(scheduler->stats().merged, 1u);
    EXPECT_EQ(scheduler->stats().elapsed, std::chrono::milliseconds(21));
    std::sort(delivered.begin(), delivered.end());
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

TEST_F(SchedulerTest, BoundsSubscribersForOneDownload) {
    tb::ReplyLimiter limiter;
    unsigned replies = 0;
    for (unsigned i = 0; i < 100; ++i) {
        scheduler->Submit({"a", "#one", "https", "same/"},
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
} // namespace
