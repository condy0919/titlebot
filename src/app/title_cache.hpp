#ifndef TITLEBOT_TITLE_CACHE_HPP_
#define TITLEBOT_TITLE_CACHE_HPP_

#include <chrono>
#include <list>
#include <optional>
#include <string>
#include <unordered_map>

#include "common/runtime_limits.hpp"
#include "http/fetcher.hpp"

namespace tb {
class TitleCache {
    using Time = std::chrono::steady_clock::time_point;

public:
    explicit TitleCache(SchedulerLimits limits) : limits_(limits) {}
    TitleCache(const TitleCache&) = delete;
    TitleCache& operator=(const TitleCache&) = delete;
    /// Return the cached outcome, including a cached failure. An expired key yields nullopt.
    std::optional<FetchResult> Find(const std::string& url, Time now);
    /// For example, Store("https://a/", result, now) makes result available until its TTL expires.
    void Store(std::string url, const FetchResult& result, Time now);

private:
    struct Entry {
        FetchResult result;
        Time expires;
        std::list<std::string>::iterator position;
    };
    SchedulerLimits limits_;
    std::list<std::string> recent_;
    std::unordered_map<std::string, Entry> entries_;
};
} // namespace tb
#endif // TITLEBOT_TITLE_CACHE_HPP_
