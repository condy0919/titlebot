#include "fetch_scheduler.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <iterator>
#include <locale>
#include <utility>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/use_awaitable.hpp>

#include "common/log.hpp"

namespace tb {
namespace asio = boost::asio;
namespace {
constexpr std::size_t kMaxActive = 8;
constexpr std::size_t kPerHost = 2;
constexpr std::size_t kQueue = 64;
constexpr std::pair<FetchError, const char*> kErrorNames[] = {
    {FetchError::kInvalidUrl, "invalid_url"},
    {FetchError::kNetwork, "network"},
    {FetchError::kTimeout, "timeouts"},
    {FetchError::kDecode, "decode"},
    {FetchError::kResponseLimit, "response_limit"},
    {FetchError::kBlockedAddress, "blocked"},
    {FetchError::kRedirectLimit, "redirect_limit"}};
} // namespace

FetchScheduler::FetchScheduler(asio::any_io_executor executor, Fetch fetch)
    : executor_(std::move(executor)), fetch_(std::move(fetch)),
      next_report_(Clock::now() + std::chrono::minutes(1)) {}

void FetchScheduler::Submit(PrivMsg message, Deliver deliver) {
    MaybeReport();
    // Bound pending replies even when many requests share a single download.
    if (subscribers_ >= kMaxActive + kQueue) {
        TB_LOG_WARN("Dropping fetch request: subscriber limit reached ({})", subscribers_);
        ++stats_.queue_drops;
        return;
    }

    // A queued or running job can deliver the same title to several callers.
    for (auto& job : jobs_) {
        if (job.message.protocol == message.protocol && job.message.url == message.url) {
            job.replies.push_back(std::move(deliver));
            ++subscribers_;
            ++stats_.merged;
            return;
        }
    }

    // Build the per-host concurrency key: EXAMPLE.org.:8080/path and example.org/x
    // share a quota.
    auto end = message.url.find_first_of(":/?#");
    std::string host = message.url.substr(0, end);
    for (char& ch : host) {
        ch = std::tolower(ch, std::locale::classic());
    }
    if (host.ends_with('.')) {
        host.pop_back();
    }
    jobs_.push_back({
        .message = std::move(message),
        .host = std::move(host),
        .replies = {std::move(deliver)},
    });
    ++subscribers_;
    // Give the new job a chance to start before applying the waiting-queue limit.
    // If the queue still overflows, discard the new arrival and keep the older jobs.
    Pump();
    if (jobs_.size() - active_ > kQueue) {
        TB_LOG_WARN("Dropping new fetch job: waiting queue is full ({} jobs)", kQueue);
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
        asio::co_spawn(
            executor_,
            [self = shared_from_this(), job, start = Clock::now()]() -> asio::awaitable<void> {
                // Even an immediately ready fetch must not erase Pump's current iterator.
                co_await asio::post(asio::use_awaitable);
                FetchResult result;
                try {
                    result = co_await self->fetch_(job->message);
                    if (!result) {
                        auto entry = std::ranges::find(kErrorNames, result.error(),
                                                       &std::pair<FetchError, const char*>::first);
                        TB_LOG_WARN("Fetch failed: {}",
                                    entry != std::end(kErrorNames) ? entry->second : "unknown");
                    }
                } catch (const std::exception& ex) {
                    TB_LOG_ERROR("Fetch threw an exception: {}", ex.what());
                    result = std::unexpected(FetchError::kNetwork);
                } catch (...) {
                    TB_LOG_ERROR("Fetch threw an unknown exception");
                    result = std::unexpected(FetchError::kNetwork);
                }
                self->Complete(job, start, result);
            },
            asio::detached);
    }
}

void FetchScheduler::Complete(std::list<Job>::iterator job, Clock::time_point start,
                              const FetchResult& result) {
    ++stats_.completed;
    stats_.elapsed += std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start);
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
    if (Clock::now() >= next_report_) {
        Report();
        next_report_ = Clock::now() + std::chrono::minutes(1);
    }
}

void FetchScheduler::Report() const {
    std::clog << "fetch active=" << active_ << " queued=" << jobs_.size() - active_
              << " peak_queue=" << stats_.peak_queue << " completed=" << stats_.completed
              << " elapsed_ms_total=" << stats_.elapsed.count() << " titles=" << stats_.titles
              << " no_title=" << stats_.no_title << " merged=" << stats_.merged
              << " queue_drops=" << stats_.queue_drops << " reply_drops=" << stats_.reply_drops;
    for (auto [error, name] : kErrorNames) {
        auto it = stats_.failures.find(error);
        std::clog << ' ' << name << '=' << (it == stats_.failures.end() ? 0 : it->second);
    }
    std::clog << '\n';
}

bool ReplyLimiter::Allow(std::string target, Clock::time_point now) {
    while (!windows_.empty() && windows_.front().expires <= now) {
        windows_.pop_front();
    }
    for (char& ch : target) {
        ch = std::tolower(ch, std::locale::classic());
    }
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
    windows_.push_back({
        .target = std::move(target),
        .expires = now + std::chrono::seconds(2),
        .sent = 1,
    });
    return true;
}
} // namespace tb
