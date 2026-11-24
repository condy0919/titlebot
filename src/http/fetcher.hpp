#ifndef TITLEBOT_FETCHER_HPP_
#define TITLEBOT_FETCHER_HPP_

#include <boost/asio/awaitable.hpp>
#include <boost/asio/ssl/context.hpp>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

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

boost::asio::awaitable<FetchResult> FetchTitle(PrivMsg msg, boost::asio::ssl::context& tls);
} // namespace tb

#endif // TITLEBOT_FETCHER_HPP_
