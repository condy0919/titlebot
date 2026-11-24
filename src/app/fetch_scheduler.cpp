#include "fetch_scheduler.hpp"
#include <algorithm>
#include <boost/asio/co_spawn.hpp>
#include <iostream>

namespace tb {
namespace {
constexpr std::size_t kMaxActive = 8;
constexpr std::size_t kPerHost = 2;
constexpr std::size_t kQueue = 64;
char LowerAscii(char ch) {
    return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + ('a' - 'A')) : ch;
}
} // namespace

FetchScheduler::FetchScheduler(boost::asio::any_io_executor executor, Fetch fetch, Now now)
    : executor_(std::move(executor)), fetch_(std::move(fetch)), now_(std::move(now)),
      next_report_(now_() + std::chrono::minutes(1)) {}

void FetchScheduler::Submit(PrivMsg message, Deliver deliver) {
    MaybeReport();
    if (subscribers_ >= kMaxActive + kQueue) {
        ++stats_.dropped;
        return;
    }
    for (auto& job : jobs_) {
        if (job.message.protocol == message.protocol && job.message.url == message.url) {
            job.replies.push_back(std::move(deliver));
            ++subscribers_;
            ++stats_.merged;
            return;
        }
    }
    std::string host = message.url.substr(0, message.url.find_first_of(":/?#"));
    std::ranges::transform(host, host.begin(), LowerAscii);
    if (host.ends_with('.')) {
        host.pop_back();
    }
    jobs_.push_back({std::move(message), std::move(host), {std::move(deliver)}});
    ++subscribers_;
    Pump();
    if (jobs_.size() - active_ > kQueue) {
        jobs_.pop_back();
        --subscribers_;
        ++stats_.dropped;
    }
    stats_.peak_queue = std::max(stats_.peak_queue, jobs_.size() - active_);
}

void FetchScheduler::Pump() {
    for (auto job = jobs_.begin(); job != jobs_.end() && active_ < kMaxActive; ++job) {
        if (job->running || std::ranges::count_if(jobs_, [&](const Job& other) {
                                return other.running && other.host == job->host;
                            }) >= static_cast<int>(kPerHost)) {
            continue;
        }
        job->running = true;
        ++active_;
        boost::asio::co_spawn(executor_, fetch_(job->message),
                              [self = shared_from_this(), job,
                               start = now_()](std::exception_ptr error, FetchResult result) {
                                  if (error) {
                                      result = std::unexpected(FetchError::kNetwork);
                                  }
                                  self->Complete(job, start, result);
                              });
    }
}

void FetchScheduler::Complete(std::list<Job>::iterator job, Clock::time_point start,
                              const FetchResult& result) {
    ++stats_.completed;
    stats_.elapsed += std::chrono::duration_cast<std::chrono::milliseconds>(now_() - start);
    if (!result && result.error() == FetchError::kTimeout) {
        ++stats_.timeouts;
    }
    auto replies = std::move(job->replies);
    jobs_.erase(job);
    --active_;
    subscribers_ -= replies.size();
    if (result && *result) {
        for (auto& deliver : replies) {
            deliver(**result);
        }
    }
    MaybeReport();
    Pump();
}

void FetchScheduler::RecordReplyDrop() {
    ++stats_.dropped;
    MaybeReport();
}

void FetchScheduler::MaybeReport() {
    if (now_() >= next_report_) {
        Report();
        next_report_ = now_() + std::chrono::minutes(1);
    }
}

void FetchScheduler::Report() const {
    std::clog << "fetch active=" << active_ << " queued=" << jobs_.size() - active_
              << " peak_queue=" << stats_.peak_queue << " completed=" << stats_.completed
              << " elapsed_ms_total=" << stats_.elapsed.count() << " timeouts=" << stats_.timeouts
              << " merged=" << stats_.merged << " dropped=" << stats_.dropped << '\n';
}

bool ReplyLimiter::Allow(std::string target, Clock::time_point now) {
    while (!windows_.empty() && windows_.front().expires <= now) {
        windows_.pop_front();
    }
    std::ranges::transform(target, target.begin(), LowerAscii);
    for (auto& window : windows_) {
        if (window.target == target) {
            if (window.sent >= 2) {
                return false;
            }
            ++window.sent;
            return true;
        }
    }
    if (windows_.size() >= 256) {
        return false;
    }
    windows_.push_back({std::move(target), now + std::chrono::seconds(2), 1});
    return true;
}
} // namespace tb
