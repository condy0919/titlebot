#ifndef TITLEBOT_FETCHER_HPP_
#define TITLEBOT_FETCHER_HPP_

#include "irc/irc.hpp"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ssl/context.hpp>
#include <expected>
#include <optional>
#include <string>

namespace tb {
enum class FetchError {
    kInvalidUrl,
    kBlockedAddress,
    kNetwork,
    kTimeout,
    kRedirectLimit,
    kDecode,
    kResponseLimit
};
using FetchResult = std::expected<std::optional<std::string>, FetchError>;
boost::asio::awaitable<FetchResult> FetchTitle(PrivMsg msg, boost::asio::ssl::context& tls);
} // namespace tb
#endif
