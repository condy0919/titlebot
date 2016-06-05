#pragma once

#include <uchardet/uchardet.h>
#include <memory>
#include <utility>

struct CharDetectorDeleter {
    void operator()(uchardet_t ud) {
        uchardet_delete(ud);
    }
};


class CharDetector {
public:
    CharDetector() noexcept : ptr_(uchardet_new()) {}

    CharDetector(CharDetector&& rhs) noexcept : ptr_(std::move(rhs.ptr_)) {}

    CharDetector& operator=(CharDetector&& rhs) noexcept {
        ptr_ = std::move(rhs.ptr_);
        return *this;
    }

    bool feed(const char* data, std::size_t sz) noexcept {
        return !uchardet_handle_data(ptr_.get(), data, sz);
    }

    void term() noexcept {
        uchardet_data_end(ptr_.get());
    }

    void reset() noexcept {
        uchardet_reset(ptr_.get());
    }

    const char* getCharset() noexcept {
        return uchardet_get_charset(ptr_.get());
    }

    void swap(CharDetector& rhs) noexcept {
        using std::swap;
        swap(ptr_, rhs.ptr_);
    }

private:
    using ObjT = std::decay_t<decltype(*std::declval<uchardet_t>())>;
    std::unique_ptr<ObjT, CharDetectorDeleter> ptr_;
};

namespace std {
inline void swap(CharDetector& lhs, CharDetector& rhs) noexcept {
    lhs.swap(rhs);
}
}
