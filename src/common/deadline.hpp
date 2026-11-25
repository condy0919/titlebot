#ifndef TITLEBOT_DEADLINE_HPP_
#define TITLEBOT_DEADLINE_HPP_

#include <memory>
#include <utility>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

namespace tb {

/// A one-shot deadline for operations on the same single-threaded executor.
/// Wait() returns true on expiry. For example, cancelling the registration deadline
/// on IRC 001 makes its pending Wait() return false and keeps the socket open.
template <class Timer>
class BasicDeadline {
public:
    template <class... Args>
    explicit BasicDeadline(Args&&... args)
        : state_(std::make_shared<State>(Timer(std::forward<Args>(args)...))) {}

    BasicDeadline(const BasicDeadline&) = delete;
    BasicDeadline& operator=(const BasicDeadline&) = delete;
    ~BasicDeadline() { Cancel(); }

    void Cancel() {
        if (!state_->cancelled) {
            state_->cancelled = true;
            state_->timer.cancel();
        }
    }

    boost::asio::awaitable<bool> Wait() const {
        return Wait(state_);
    }

private:
    struct State {
        Timer timer;
        bool cancelled = false;
    };

    static boost::asio::awaitable<bool> Wait(std::shared_ptr<State> state) {
        // The owner may cancel before co_spawn starts this coroutine.
        if (state->cancelled) {
            co_return false;
        }
        boost::system::error_code ec;
        co_await state->timer.async_wait(
            boost::asio::redirect_error(boost::asio::use_awaitable, ec));
        co_return !ec && !state->cancelled;
    }

    std::shared_ptr<State> state_;
};

using Deadline = BasicDeadline<boost::asio::steady_timer>;

} // namespace tb

#endif // TITLEBOT_DEADLINE_HPP_
