#ifndef TITLEBOT_FETCH_SCHEDULER_HPP_
#define TITLEBOT_FETCH_SCHEDULER_HPP_

#include <chrono>
#include <list>
#include <map>
#include <memory>
#include <vector>

#include <boost/asio/any_io_executor.hpp>

#include "server.hpp"

namespace tb {
using Clock = std::chrono::steady_clock;
using Now = std::function<Clock::time_point()>;

/// The single-threaded IRC executor shares this download budget across sessions.
class FetchScheduler : public std::enable_shared_from_this<FetchScheduler> {
public:
    using Deliver = std::function<void(const std::string&)>;

    FetchScheduler(boost::asio::any_io_executor executor, Fetch fetch, Now now = Clock::now);

    void Submit(PrivMsg message, Deliver deliver);
    void RecordReplyDrop();
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
    Now now_;
    Clock::time_point next_report_;
    std::list<Job> jobs_;
    std::size_t active_ = 0;
    std::size_t subscribers_ = 0;
    Stats stats_;
};

/// Track a fixed window for each IRC target and discard expired entries.
class ReplyLimiter {
public:
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
