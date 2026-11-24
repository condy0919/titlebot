#include "fetch_scheduler.hpp"

#include <algorithm>
#include <iostream>

#include <boost/asio/co_spawn.hpp>

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
        ++stats_.queue_drops;
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
    auto end =
        message.url.starts_with('[') ? message.url.find(']') : message.url.find_first_of(":/?#");
    std::string host = message.url.substr(0, end);
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
        ++stats_.queue_drops;
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
    if (!result) {
        ++stats_.failures[result.error()];
    } else if (result->has_value()) {
        ++stats_.titles;
    } else {
        ++stats_.no_title;
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
    ++stats_.reply_drops;
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
              << " elapsed_ms_total=" << stats_.elapsed.count() << " titles=" << stats_.titles
              << " no_title=" << stats_.no_title << " merged=" << stats_.merged
              << " queue_drops=" << stats_.queue_drops << " reply_drops=" << stats_.reply_drops;
    for (auto [error, name] : {std::pair{FetchError::kInvalidUrl, "invalid_url"},
                               {FetchError::kNetwork, "network"},
                               {FetchError::kTimeout, "timeouts"},
                               {FetchError::kDecode, "decode"},
                               {FetchError::kResponseLimit, "response_limit"},
                               {FetchError::kBlockedAddress, "blocked"},
                               {FetchError::kRedirectLimit, "redirect_limit"}}) {
        auto it = stats_.failures.find(error);
        std::clog << ' ' << name << '=' << (it == stats_.failures.end() ? 0 : it->second);
    }
    std::clog << '\n';
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
