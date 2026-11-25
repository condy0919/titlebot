#ifndef IRCBOT_MANUAL_CLOCK_HPP_
#define IRCBOT_MANUAL_CLOCK_HPP_

#include <chrono>
#include <expected>
#include <memory>
#include <vector>

namespace ircbot {
namespace detail {
class SessionTimer;
}

// Advance on the same executor that runs the client. Each instance has its own time domain.
enum class ClockError { kInvalidAdvance };

class ManualClock {
public:
    ManualClock() = default;
    ManualClock(const ManualClock&) = delete;
    ManualClock& operator=(const ManualClock&) = delete;
    using TimePoint = std::chrono::steady_clock::time_point;
    TimePoint Now() const {
        return now_;
    }
    /// For example, Advance(30s) makes a registration timeout at Now() + 30s ready.
    std::expected<void, ClockError> Advance(std::chrono::steady_clock::duration elapsed);

private:
    friend class detail::SessionTimer;
    struct Alarm {
        virtual ~Alarm() = default;
        virtual void Advance(TimePoint now) = 0;
    };
    std::vector<std::weak_ptr<Alarm>> alarms_;
    TimePoint now_{};
};
} // namespace ircbot
#endif // IRCBOT_MANUAL_CLOCK_HPP_
