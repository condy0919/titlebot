#ifndef TITLEBOT_FETCH_SCHEDULER_HPP_
#define TITLEBOT_FETCH_SCHEDULER_HPP_

#include <chrono>
#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

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
    FetchScheduler(boost::asio::any_io_executor executor, Fetch fetch);

    /// Queue a fetch and call deliver if it yields a title. For example, two accepted
    /// Submit(msg, deliver) calls for the same pending URL fetch it once and deliver twice.
    void Submit(PrivMsg message, Deliver deliver);

    /// Count a dropped outgoing reply. RecordReplyDrop() increments stats().reply_drops by one.
    void RecordReplyDrop();

    /// Log current counters. For example, Report() includes completed=0
    /// before the first fetch completes.
    void Report() const;

    struct Stats {
        std::size_t completed = 0;
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
        bool running = false;
    };

    void Pump();
    void Complete(std::list<Job>::iterator job, Clock::time_point start, const FetchResult& result);
    void MaybeReport();

    boost::asio::any_io_executor executor_;
    Fetch fetch_;
    Clock::time_point next_report_;
    std::list<Job> jobs_;
    std::map<std::string, std::size_t> host_active_;
    std::size_t active_ = 0;
    std::size_t subscribers_ = 0;
    Stats stats_;
};

/// Track a fixed window for each IRC target and discard expired entries.
class ReplyLimiter {
public:
    /// Consume one of two replies allowed per target in a two-second window.
    /// For example, three calls to Allow("#room", now) on a fresh limiter yield true, true, false.
    bool Allow(std::string target, Clock::time_point now);

private:
    struct Window {
        std::string target;
        Clock::time_point expires;
        unsigned sent;
    };

    std::list<Window> windows_;
};
} // namespace tb

#endif // TITLEBOT_FETCH_SCHEDULER_HPP_
