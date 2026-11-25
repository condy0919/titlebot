#ifndef TITLEBOT_RUNTIME_LIMITS_HPP_
#define TITLEBOT_RUNTIME_LIMITS_HPP_

#include <chrono>
#include <cstddef>

namespace tb {
struct FetchLimits {
    std::chrono::milliseconds total_timeout = std::chrono::seconds(10);
    std::chrono::milliseconds connect_timeout = std::chrono::seconds(3);
    std::size_t body_bytes = 512 * 1024;
};

struct SchedulerLimits {
    std::size_t active = 8;
    std::size_t per_host = 2;
    std::size_t queued = 64;
    std::size_t cache_entries = 256;
    std::chrono::milliseconds title_ttl = std::chrono::minutes(4);
    std::chrono::milliseconds negative_ttl = std::chrono::seconds(30);
};

struct ReplyLimits {
    std::chrono::milliseconds window = std::chrono::seconds(2);
    unsigned per_target = 2;
    unsigned per_session = 8;
    std::size_t targets = 256;
    std::size_t queued = 64;
    std::size_t line_bytes = 430;
};
} // namespace tb

#endif // TITLEBOT_RUNTIME_LIMITS_HPP_
