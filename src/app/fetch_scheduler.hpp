#ifndef TITLEBOT_FETCH_SCHEDULER_HPP_
#define TITLEBOT_FETCH_SCHEDULER_HPP_

#include <chrono>
#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>

#include "common/runtime_limits.hpp"
#include "http/fetcher.hpp"
#include "irc/irc.hpp"

namespace tb {
using Clock = std::chrono::steady_clock;

/// The single-threaded IRC executor shares this download budget across sessions.
class FetchScheduler : public std::enable_shared_from_this<FetchScheduler> {
public:
    using Fetch = std::function<boost::asio::awaitable<FetchResult>(PrivMsg)>;
    using Deliver = std::function<void(const std::string&)>;

    /// Create with std::make_shared<FetchScheduler>(executor, fetch) so active jobs can keep
    /// the scheduler alive until their coroutines finish delivering results.
    FetchScheduler(boost::asio::any_io_executor executor, Fetch fetch, SchedulerLimits limits = {});

    struct Destination {
        std::weak_ptr<void> session;
        std::string target;
    };
    /// 为消息抓取标题。相同链接共用一次下载，同一会话的同一目标仅接收一次在途结果。
    /// 例如 #a 和 #b 同时提交相同链接时，两个频道各收到一次回复。
    void Submit(PrivMsg message, Deliver deliver, Destination destination = {});

    /// Count a dropped outgoing reply. RecordReplyDrop() increments stats().reply_drops by one.
    void RecordReplyDrop();

    /// 记录抓取统计。例如，首个任务完成前汇总包含 completed=0。
    void Report() const;

    /// 启用按分钟汇总，空闲期间没有统计变化时不打印。
    void EnableReports();

    struct Stats {
        std::size_t completed = 0;
        std::size_t cache_hits = 0;
        std::size_t duplicates = 0;
        std::size_t titles = 0;
        std::size_t no_title = 0;
        std::map<FetchError, std::size_t> failures;
        std::size_t queue_drops = 0;
        std::size_t reply_drops = 0;
        std::size_t merged = 0;
        std::size_t peak_queue = 0;
        std::chrono::milliseconds elapsed{};
    };

    const Stats& stats() const {
        return stats_;
    }

private:
    struct Cached {
        std::string protocol;
        std::string url;
        FetchResult result;
        Clock::time_point expires;
    };
    std::list<Cached> cache_;
    struct Job {
        PrivMsg message;
        std::string host;
        std::vector<Deliver> replies;
        std::vector<Destination> destinations;
        bool running = false;
    };

    void Pump();
    void Complete(std::list<Job>::iterator job, Clock::time_point start, const FetchResult& result);
    void ScheduleReport();

    boost::asio::any_io_executor executor_;
    Fetch fetch_;
    SchedulerLimits limits_;
    std::optional<boost::asio::steady_timer> report_timer_;
    bool report_pending_ = false;
    std::list<Job> jobs_;
    std::map<std::string, std::size_t> host_active_;
    std::size_t active_ = 0;
    std::size_t subscribers_ = 0;
    Stats stats_;
};

/// Track a fixed window for each IRC target and discard expired entries.
class ReplyLimiter {
public:
    ReplyLimiter(ReplyLimits limits = {}) : limits_(limits) {}

    /// Consume one of two replies allowed per target in a two-second window.
    /// For example, three calls to Allow("#room", now) on a fresh limiter yield true, true, false.
    bool Allow(std::string target, Clock::time_point now);

private:
    struct Window {
        std::string target;
        Clock::time_point expires;
        unsigned sent;
    };

    ReplyLimits limits_;
    std::list<Window> windows_;
};
} // namespace tb

#endif // TITLEBOT_FETCH_SCHEDULER_HPP_
