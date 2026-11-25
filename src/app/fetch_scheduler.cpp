#include "fetch_scheduler.hpp"

#include <algorithm>
#include <exception>
#include <sstream>
#include <iterator>
#include <utility>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/url/grammar/ci_string.hpp>

#include "common/log.hpp"
#include "irc/irc.hpp"

namespace tb {
namespace asio = boost::asio;
namespace {
constexpr std::pair<FetchError, const char*> kErrorNames[] = {
    {FetchError::kInvalidUrl, "invalid_url"},
    {FetchError::kNetwork, "network"},
    {FetchError::kTimeout, "timeouts"},
    {FetchError::kDecode, "decode"},
    {FetchError::kResponseLimit, "response_limit"},
    {FetchError::kBlockedAddress, "blocked"},
    {FetchError::kRedirectLimit, "redirect_limit"}};
} // namespace

FetchScheduler::FetchScheduler(asio::any_io_executor executor, Fetch fetch, SchedulerLimits limits)
    : executor_(std::move(executor)), fetch_(std::move(fetch)), limits_(limits) {}

void FetchScheduler::Submit(PrivMsg message, Deliver deliver, Destination destination) {
    auto cached = std::ranges::find_if(cache_, [&](const Cached& entry) {
        return entry.protocol == message.protocol && entry.url == message.url;
    });
    if (cached != cache_.end()) {
        if (Clock::now() < cached->expires) {
            ++stats_.cache_hits;
            ScheduleReport();
            cache_.splice(cache_.begin(), cache_, cached);
            if (cached->result && *cached->result) {
                deliver(**cached->result);
            }
            return;
        }
        cache_.erase(cached);
    }
    ScheduleReport();
    // Bound pending replies even when many requests share a single download.
    if (!limits_.per_host || !limits_.active || subscribers_ >= limits_.active + limits_.queued) {
        TB_LOG_WARN("Dropping fetch request: subscriber limit reached ({})", subscribers_);
        ++stats_.queue_drops;
        return;
    }

    // A queued or running job can deliver the same title to several callers.
    for (auto& job : jobs_) {
        if (job.message.protocol == message.protocol && job.message.url == message.url) {
            if (!destination.session.expired() && std::ranges::any_of(
                    job.destinations, [&](const Destination& existing) {
                        return !existing.session.owner_before(destination.session) &&
                               !destination.session.owner_before(existing.session) &&
                               std::ranges::equal(existing.target, destination.target, {},
                                                  FoldIrcCase, FoldIrcCase);
                    })) {
                ++stats_.duplicates;
                return;
            }
            job.destinations.push_back(std::move(destination));
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
        ch = boost::urls::grammar::to_lower(ch);
    }
    if (host.ends_with('.')) {
        host.pop_back();
    }
    if (host.empty() || host.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789.-") != host.npos ||
        message.url.substr(0, message.url.find_first_of("/?#")).contains("@")) {
        host = "<invalid-host>";
    }
    jobs_.push_back({
        .message = std::move(message),
        .host = std::move(host),
        .replies = {std::move(deliver)},
        .destinations = {std::move(destination)},
    });
    ++subscribers_;
    // Give the new job a chance to start before applying the waiting-queue limit.
    // If the queue still overflows, discard the new arrival and keep the older jobs.
    Pump();
    if (jobs_.size() - active_ > limits_.queued) {
        TB_LOG_WARN("Dropping new fetch job: waiting queue is full ({} jobs)", limits_.queued);
        jobs_.pop_back();
        --subscribers_;
        ++stats_.queue_drops;
    }
    stats_.peak_queue = std::max(stats_.peak_queue, jobs_.size() - active_);
}

void FetchScheduler::Pump() {
    for (auto job = jobs_.begin(); job != jobs_.end() && active_ < limits_.active; ++job) {
        if (job->running || host_active_[job->host] >= limits_.per_host) {
            continue;
        }
        job->running = true;
        ++host_active_[job->host];
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
                        TB_LOG_WARN("Fetch failed for {}: {}", job->host,
                                    entry != std::end(kErrorNames) ? entry->second : "unknown");
                    }
                } catch (const std::exception& ex) {
                    TB_LOG_ERROR("Fetch threw an exception for {}: {}", job->host, ex.what());
                    result = std::unexpected(FetchError::kNetwork);
                } catch (...) {
                    TB_LOG_ERROR("Fetch threw an unknown exception for {}", job->host);
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
    const auto ttl = result && *result ? std::chrono::minutes(4) : std::chrono::seconds(30);
    cache_.push_front({job->message.protocol, job->message.url, result, Clock::now() + ttl});
    if (cache_.size() > 256) {
        cache_.pop_back();
    }
    auto replies = std::move(job->replies);
    auto host = host_active_.find(job->host);
    if (--host->second == 0) {
        host_active_.erase(host);
    }
    jobs_.erase(job);
    --active_;
    subscribers_ -= replies.size();
    if (result && *result) {
        for (auto& deliver : replies) {
            deliver(**result);
        }
    }
    ScheduleReport();
    Pump();
}

void FetchScheduler::RecordReplyDrop() {
    ++stats_.reply_drops;
    ScheduleReport();
}

void FetchScheduler::EnableReports() {
    report_timer_.emplace(executor_);
}

void FetchScheduler::ScheduleReport() {
    if (!report_timer_ || report_pending_) {
        return;
    }
    report_pending_ = true;
    report_timer_->expires_after(std::chrono::minutes(1));
    report_timer_->async_wait([weak = weak_from_this()](boost::system::error_code ec) {
        if (auto self = weak.lock(); self && !ec) {
            self->report_pending_ = false;
            self->Report();
        }
    });
}

void FetchScheduler::Report() const {
    std::ostringstream summary;
    summary << "fetch active=" << active_ << " queued=" << jobs_.size() - active_
              << " peak_queue=" << stats_.peak_queue << " completed=" << stats_.completed
              << " cache_hits=" << stats_.cache_hits << " duplicate_replies=" << stats_.duplicates
              << " elapsed_ms_total=" << stats_.elapsed.count() << " titles=" << stats_.titles
              << " no_title=" << stats_.no_title << " merged=" << stats_.merged
              << " queue_drops=" << stats_.queue_drops << " reply_drops=" << stats_.reply_drops;
    for (auto [error, name] : kErrorNames) {
        auto it = stats_.failures.find(error);
        summary << ' ' << name << '=' << (it == stats_.failures.end() ? 0 : it->second);
    }
    TB_LOG_INFO("{}", summary.str());
}

bool ReplyLimiter::Allow(std::string target, Clock::time_point now) {
    while (!windows_.empty() && windows_.front().expires <= now) {
        windows_.pop_front();
    }
    for (char& ch : target) {
        ch = FoldIrcCase(ch);
    }
    for (auto& window : windows_) {
        if (window.target == target) {
            if (window.sent >= limits_.per_target) {
                return false;
            }
            ++window.sent;
            return true;
        }
    }
    if (!limits_.per_target || windows_.size() >= limits_.targets) {
        return false;
    }
    windows_.push_back({
        .target = std::move(target),
        .expires = now + limits_.window,
        .sent = 1,
    });
    return true;
}
} // namespace tb
