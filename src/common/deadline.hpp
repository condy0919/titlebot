#ifndef TITLEBOT_DEADLINE_HPP_
#define TITLEBOT_DEADLINE_HPP_

#include <memory>

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
class Deadline {
public:
    Deadline(const boost::asio::any_io_executor& executor,
             boost::asio::steady_timer::time_point expires)
        : state_(std::make_shared<State>(boost::asio::steady_timer(executor, expires))) {}

    Deadline(const Deadline&) = delete;
    Deadline& operator=(const Deadline&) = delete;

    ~Deadline() {
        Cancel();
    }

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
        boost::asio::steady_timer timer;
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

} // namespace tb

#endif // TITLEBOT_DEADLINE_HPP_
