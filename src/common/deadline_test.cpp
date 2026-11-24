#include "deadline.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <utility>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

namespace {
namespace asio = boost::asio;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

class DeadlineTest : public testing::Test {
protected:
    void ExpectResult(std::future<bool>& result, bool expired) {
        io.run_for(1s);
        ASSERT_EQ(result.wait_for(0s), std::future_status::ready);
        EXPECT_EQ(result.get(), expired);
    }

    asio::io_context io;
};

TEST_F(DeadlineTest, ReportsExpiry) {
    tb::Deadline deadline(io.get_executor(), Clock::now());
    auto result = asio::co_spawn(io, deadline.Wait(), asio::use_future);
    ExpectResult(result, true);
}

TEST_F(DeadlineTest, RemembersCancellationBeforeWaitStarts) {
    tb::Deadline deadline(io.get_executor(), Clock::time_point::max());
    auto wait = deadline.Wait();
    deadline.Cancel();
    deadline.Cancel();
    auto result = asio::co_spawn(io, std::move(wait), asio::use_future);
    ExpectResult(result, false);
}

TEST_F(DeadlineTest, CancelsPendingWait) {
    tb::Deadline deadline(io.get_executor(), Clock::time_point::max());
    auto result = asio::co_spawn(io, deadline.Wait(), asio::use_future);
    io.poll();
    ASSERT_EQ(result.wait_for(0s), std::future_status::timeout);
    deadline.Cancel();
    ExpectResult(result, false);
}

TEST_F(DeadlineTest, DestructionCancelsPendingWait) {
    std::future<bool> result;
    {
        tb::Deadline deadline(io.get_executor(), Clock::time_point::max());
        result = asio::co_spawn(io, deadline.Wait(), asio::use_future);
        io.poll();
        ASSERT_EQ(result.wait_for(0s), std::future_status::timeout);
    }
    ExpectResult(result, false);
}
} // namespace
