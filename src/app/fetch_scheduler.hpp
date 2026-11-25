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
#include "ircbot/features.hpp"
#include "title_cache.hpp"

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
        ircbot::CaseMapping mapping = ircbot::CaseMapping::kRfc1459;
    };
    /// Fetch a title once for pending requests for the same link.
    /// For example, simultaneous submissions from #a and #b deliver once to each channel.
    void Submit(PrivMsg message, Deliver deliver, Destination destination);
    void Submit(PrivMsg message, Deliver deliver);

    /// Count a dropped outgoing reply. RecordReplyDrop() increments stats().reply_drops by one.
    void RecordReplyDrop();

    /// Report current counters. Before any download completes, Report() logs completed=0.
    void Report() const;

    /// After EnableReports(), a completed download triggers a summary within one minute.
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
    struct Job {
        PrivMsg message;
        std::string host;
        std::vector<Deliver> replies;
        std::vector<Destination> destinations;
        bool running = false;
    };

    void Pump();
    void PruneExpiredSubscribers();
    void Complete(std::list<Job>::iterator job, Clock::time_point start, const FetchResult& result);
    void ScheduleReport();

    boost::asio::any_io_executor executor_;
    Fetch fetch_;
    SchedulerLimits limits_;
    TitleCache cache_;
    std::optional<boost::asio::steady_timer> report_timer_;
    bool report_pending_ = false;
    std::list<Job> jobs_;
    std::map<std::string, std::size_t> host_active_;
    std::size_t active_ = 0;
    std::size_t subscribers_ = 0;
    Stats stats_;
};

} // namespace tb

#endif // TITLEBOT_FETCH_SCHEDULER_HPP_
