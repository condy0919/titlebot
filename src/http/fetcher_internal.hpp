#ifndef TITLEBOT_FETCHER_INTERNAL_HPP_
#define TITLEBOT_FETCHER_INTERNAL_HPP_

#include <chrono>
#include <functional>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>

#include "fetcher.hpp"
#include "irc/irc.hpp"

namespace tb {
namespace detail {
/// Check a resolved address before connecting. For example, 127.0.0.1 returns false.
bool IsPublicAddress(boost::asio::ip::address address);

struct FetchPolicy {
    std::function<bool(boost::asio::ip::tcp::endpoint)> allow;
    std::chrono::milliseconds total_timeout;
    std::chrono::milliseconds connect_timeout;
    std::size_t body_bytes = FetchLimits{}.body_bytes;
};

/// Check each resolved endpoint against policy.allow, including after redirects. The total
/// timeout covers the entire fetch. For example, co_await FetchWithPolicy(msg, tls, policy)
/// returns kBlockedAddress if the policy rejects an endpoint for the requested HTTP URL.
boost::asio::awaitable<FetchResult> FetchWithPolicy(PrivMsg msg, boost::asio::ssl::context& tls,
                                                    FetchPolicy policy);
} // namespace detail
} // namespace tb

#endif // TITLEBOT_FETCHER_INTERNAL_HPP_
