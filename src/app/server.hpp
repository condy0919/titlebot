#ifndef TITLEBOT_SERVER_HPP_
#define TITLEBOT_SERVER_HPP_
#include "http/fetcher.hpp"
#include <boost/asio/ip/tcp.hpp>
#include <functional>

namespace tb {
using Fetch = std::function<boost::asio::awaitable<FetchResult>(PrivMsg)>;
boost::asio::awaitable<void> RunBot(std::string host, std::string port, std::string nick,
                                    std::string channels, Fetch fetch);
boost::asio::awaitable<bool> RunSession(boost::asio::ip::tcp::socket socket, std::string nick,
                                        std::string channels, Fetch fetch);
} // namespace tb
#endif
