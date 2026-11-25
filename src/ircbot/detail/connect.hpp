#ifndef IRCBOT_DETAIL_CONNECT_HPP_
#define IRCBOT_DETAIL_CONNECT_HPP_
#include <chrono>
#include <memory>

#include <boost/asio.hpp>

#include "deadline.hpp"
namespace ircbot::detail {
/// Connect to the first reachable endpoint, dividing the remaining time among candidates.
/// For example, ConnectEndpoints(socket, answers, now + 3s) tries each address within 3s.
template <class Endpoints>
boost::asio::awaitable<void> ConnectEndpoints(std::shared_ptr<boost::asio::ip::tcp::socket> socket,
                                              const Endpoints& answers,
                                              std::chrono::steady_clock::time_point expires) {
    namespace asio = boost::asio;
    using asio::use_awaitable;
    auto executor = socket->get_executor();
    boost::system::error_code ec = asio::error::host_unreachable;
    auto remaining_endpoints = answers.size();
    for (const auto& answer : answers) {
        if (std::chrono::steady_clock::now() >= expires) {
            throw boost::system::system_error(asio::error::timed_out);
        }
        socket->close();
        const auto now = std::chrono::steady_clock::now();
        Deadline attempt(executor,
                         now + (expires - now) / static_cast<long>(remaining_endpoints--));
        asio::co_spawn(
            executor,
            [socket, wait = attempt.Wait()]() mutable -> asio::awaitable<void> {
                if (co_await std::move(wait)) {
                    boost::system::error_code ignored;
                    socket->cancel(ignored);
                }
            },
            asio::detached);
        co_await socket->async_connect(static_cast<boost::asio::ip::tcp::endpoint>(answer),
                                       asio::redirect_error(use_awaitable, ec));
        if (!ec) {
            break;
        }
    }
    if (ec) {
        throw boost::system::system_error(ec);
    }
}
} // namespace ircbot::detail
#endif // IRCBOT_DETAIL_CONNECT_HPP_
