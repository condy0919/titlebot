#ifndef TITLEBOT_FETCHER_HPP_
#define TITLEBOT_FETCHER_HPP_

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ssl/context.hpp>

#include "common/runtime_limits.hpp"
#include "irc/irc.hpp"

namespace tb {
enum class FetchError : std::uint8_t {
    kInvalidUrl,
    kBlockedAddress,
    kNetwork,
    kTimeout,
    kRedirectLimit,
    kDecode,
    kResponseLimit,
};

using FetchResult = std::expected<std::optional<std::string>, FetchError>;

/// Fetch a public HTTP(S) page. Success contains its title or std::nullopt if none was
/// obtained. For example, co_await FetchTitle({"a", "#r", "http", "127.0.0.1/"}, tls)
/// returns std::unexpected(FetchError::kBlockedAddress).
boost::asio::awaitable<FetchResult> FetchTitle(PrivMsg msg, boost::asio::ssl::context& tls, FetchLimits limits = {});
} // namespace tb

#endif // TITLEBOT_FETCHER_HPP_
