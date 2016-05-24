#pragma once

#include <list>
#include <utility>
#include <unordered_map>
#include <experimental/optional>

template <typename KeyT, typename ValueT, std::size_t Size>
class LruCache {
public:
    bool touch(const KeyT& k) {
        if (vs.count(k) > 0) {
            auto pair_ = vs[k];
            ks.splice(ks.begin(), ks, pair_.second);
            pair_.second = ks.begin();
            vs[k] = pair_;
            return true;
        }
        return false;
    }

    std::experimental::optional<ValueT> get(const KeyT& k) {
        if (!touch(k)) {
            return {};
        }
        return vs[k].first;
    }

    void insert(const KeyT& k, const ValueT& v) {
        if (!touch(k)) {
            if (full()) {
                auto rmd = ks.back();
                remove(rmd);
            }

            auto iter = ks.insert(ks.begin(), k);
            vs[k] = std::make_pair(v, iter);
        }
    }

    void remove(const KeyT& k) {
        if (vs.count(k) > 0) {
            auto pair_ = vs[k];
            auto iter = pair_.second;
            ks.erase(iter);
            vs.erase(k);
        }
    }

    void clear() {
        ks.clear();
        vs.clear();
    }

    bool full() const {
        return size() == Size;
    }

    bool empty() const {
        return size() == 0;
    }

    std::size_t size() const {
        return ks.size();
    }

private:
    std::list<KeyT> ks;
    std::unordered_map<KeyT, std::pair<ValueT, typename std::list<KeyT>::iterator>> vs;
};

