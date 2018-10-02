#pragma once

#include <boost/asio.hpp>
#include <boost/optional.hpp>
#include <tuple>
#include <chrono>
#include <string>
#include <utility>
#include <functional>
#include <unordered_map>

namespace std {
template <>
struct hash<std::pair<std::string, std::string>> {
    typedef std::size_t result_type;
    typedef std::pair<std::string, std::string> argument_type;

    result_type operator()(
        const std::pair<std::string, std::string>& pair_) const {
        return std::hash<std::string>()(pair_.first + ':' + pair_.second);
    }
};

}


template <typename KeyT,
          typename ValueT,
          std::size_t Expire,
          typename Hash = std::hash<KeyT>,
          typename Pred = std::equal_to<KeyT>,
          typename Alloc = std::allocator<std::pair<const KeyT, std::tuple<ValueT, std::time_t>>>>
class Cache {
private:
    using RecordsT = std::unordered_map<KeyT, std::tuple<ValueT, std::time_t>,
                                        Hash, Pred, Alloc>;

public:
    typedef KeyT key_type;
    typedef ValueT mapped_type;
    typedef std::pair<const key_type, std::tuple<ValueT, std::time_t>> value_type;
    typedef Alloc allocator_type;
    // missing key_compare/value_compare
    typedef typename allocator_type::reference reference;
    typedef typename allocator_type::const_reference const_reference;
    typedef typename allocator_type::pointer pointer;
    typedef typename allocator_type::const_pointer const_pointer;
    typedef typename RecordsT::iterator iterator;
    typedef typename RecordsT::const_iterator const_iterator;
    typedef typename RecordsT::difference_type difference_type;
    typedef typename RecordsT::size_type size_type;

    Cache() = default;

    bool touch(iterator iter) {
        auto& tp = iter->second;
        const std::time_t ts = now();
        // expires
        if (std::get<1>(tp) + Expire <= static_cast<std::size_t>(ts)) {
            records_.erase(iter);
            return false;
        }
        std::get<1>(tp) = ts;
        return true;
    }

    bool touch(const KeyT& key) {
        auto iter = records_.find(key);
        if (iter == records_.end()) {
            return false;
        }
        return touch(iter);
    }

    boost::optional<ValueT> get(iterator iter) {
        auto& tp = iter->second;
        const std::time_t ts = now();
        if (std::get<1>(tp) + Expire <= static_cast<std::size_t>(ts)) {
            records_.erase(iter);
            return {};
        }
        std::get<1>(tp) = ts;
        return boost::optional<ValueT>(std::get<0>(tp));
    }

    boost::optional<ValueT> get(const KeyT& key) {
        auto iter = records_.find(key);
        if (iter == records_.end()) {
            return {};
        }
        return get(iter);
    }

    void put(const KeyT& key, const ValueT& value) {
        if (!touch(key)) {
            records_.emplace(key, std::make_tuple(value, now()));
        }
    }

    void erase(const KeyT& key) {
        records_.erase(key);
    }

    bool empty() const noexcept {
        return records_.empty();
    }

    std::size_t size() const noexcept {
        return records_.size();
    }

    void clear() noexcept {
        records_.clear();
    }

private:
    std::time_t now() const noexcept {
        const auto now = std::chrono::system_clock::now();
        std::time_t tt = std::chrono::system_clock::to_time_t(now);
        return tt;
    }

    RecordsT records_;
};


template <std::size_t Expire>
class DNSCache {
public:
    DNSCache() = default;

    auto get(const std::string& serv, const std::string& host) {
        return cache_.get(std::make_pair(serv, host));
    }

    void put(const std::string& serv, const std::string& host,
             boost::asio::ip::tcp::resolver::iterator iter) {
        cache_.put(std::make_pair(serv, host), iter);
    }

    void erase(const std::string& serv, const std::string& host) {
        cache_.erase(std::make_pair(serv, host));
    }

    bool empty() const noexcept {
        return cache_.empty();
    }

    std::size_t size() const noexcept {
        return cache_.size();
    }

    void clear() noexcept {
        cache_.clear();
    }

private:
    Cache<std::pair<std::string, std::string>,
          boost::asio::ip::tcp::resolver::iterator, Expire>
        cache_;
};
