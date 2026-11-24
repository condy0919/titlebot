#ifndef TITLEBOT_FETCHER_INTERNAL_HPP_
#define TITLEBOT_FETCHER_INTERNAL_HPP_

#include "fetcher.hpp"
#include <boost/asio/ip/tcp.hpp>
#include <chrono>
#include <functional>

namespace tb {
namespace detail {
struct FetchPolicy {
    std::function<bool(boost::asio::ip::tcp::endpoint)> allow;
    std::chrono::milliseconds total_timeout;
    std::chrono::milliseconds connect_timeout;
};
boost::asio::awaitable<FetchResult> FetchWithPolicy(PrivMsg msg, boost::asio::ssl::context& tls,
                                                    FetchPolicy policy);
} // namespace detail
} // namespace tb
#endif
