#include "title_cache.hpp"

namespace tb {
std::optional<FetchResult> TitleCache::Find(const std::string& url, Time now) {
    auto it = entries_.find(url);
    if (it == entries_.end()) {
        return {};
    }
    if (now >= it->second.expires) {
        recent_.erase(it->second.position);
        entries_.erase(it);
        return {};
    }
    recent_.splice(recent_.begin(), recent_, it->second.position);
    return it->second.result;
}

void TitleCache::Store(std::string url, const FetchResult& result, Time now) {
    auto ttl = result && *result ? limits_.title_ttl : limits_.negative_ttl;
    if (auto previous = entries_.find(url); previous != entries_.end()) {
        recent_.erase(previous->second.position);
        entries_.erase(previous);
    }
    if (!limits_.cache_entries || ttl <= std::chrono::milliseconds::zero()) {
        return;
    }
    if (entries_.size() >= limits_.cache_entries) {
        std::erase_if(entries_, [&](const auto& item) {
            if (item.second.expires > now) {
                return false;
            }
            recent_.erase(item.second.position);
            return true;
        });
        if (entries_.size() >= limits_.cache_entries) {
            entries_.erase(recent_.back());
            recent_.pop_back();
        }
    }
    recent_.push_front(std::move(url));
    try {
        entries_.emplace(recent_.front(), Entry{result, now + ttl, recent_.begin()});
    } catch (...) {
        recent_.pop_front();
        throw;
    }
}
} // namespace tb
