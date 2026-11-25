#ifndef IRCBOT_SESSION_TIMER_HPP_
#define IRCBOT_SESSION_TIMER_HPP_

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <utility>

#include <boost/asio.hpp>

#include "common/deadline.hpp"
#include "ircbot/manual_clock.hpp"

namespace ircbot::detail {
using TimePoint = std::chrono::steady_clock::time_point;

inline TimePoint Now(const std::shared_ptr<ManualClock>& clock) {
    return clock ? clock->Now() : std::chrono::steady_clock::now();
}

class SessionTimer {
    struct State final : ManualClock::Alarm {
        explicit State(boost::asio::any_io_executor executor) : timer(std::move(executor)) {}
        boost::asio::steady_timer timer;
        TimePoint expiry = TimePoint::max();
        std::size_t generation = 0;
        bool due = false;

        void Advance(TimePoint now) override {
            if (!due && expiry <= now) {
                due = true;
                timer.expires_at(std::chrono::steady_clock::now());
            }
        }
    };

public:
    SessionTimer(boost::asio::any_io_executor executor, std::shared_ptr<ManualClock> clock,
                 TimePoint expiry = TimePoint::max())
        : state_(std::make_shared<State>(std::move(executor))), clock_(std::move(clock)) {
        if (clock_) {
            std::erase_if(clock_->alarms_, [](const auto& alarm) { return alarm.expired(); });
            clock_->alarms_.push_back(state_);
        }
        expires_at(expiry);
    }
    SessionTimer(SessionTimer&&) = default;
    ~SessionTimer() {
        if (state_) {
            cancel();
        }
    }

    void cancel() {
        ++state_->generation;
        state_->expiry = TimePoint::max();
        state_->due = false;
        state_->timer.cancel();
    }
    void expires_at(TimePoint when) {
        ++state_->generation;
        state_->expiry = when;
        state_->due = false;
        state_->timer.expires_at(clock_ ? TimePoint::max() : when);
        if (clock_) {
            state_->Advance(clock_->Now());
        }
    }
    void expires_after(std::chrono::milliseconds duration) {
        expires_at(tb::DeadlineAfter(Now(clock_), duration));
    }

    template <class Token>
    auto async_wait(Token&& token) {
        namespace asio = boost::asio;
        if (!clock_) {
            return state_->timer.async_wait(std::forward<Token>(token));
        }
        return asio::async_compose<Token, void(boost::system::error_code)>(
            [state = state_, generation = state_->generation,
             started = false](auto& self, boost::system::error_code ec = {}) mutable {
                if (!started) {
                    started = true;
                    auto& timer = state->timer;
                    timer.async_wait(std::move(self));
                    return;
                }
                // Advancing manual time wakes a pending native wait via expires_at.
                // Explicit cancellation or rearming advances the generation instead.
                if (ec == asio::error::operation_aborted && state->due &&
                    state->generation == generation) {
                    ec.clear();
                }
                self.complete(ec);
            },
            token, state_->timer);
    }

private:
    std::shared_ptr<State> state_;
    std::shared_ptr<ManualClock> clock_;
};

} // namespace ircbot::detail
#endif // IRCBOT_SESSION_TIMER_HPP_
