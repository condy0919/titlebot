#ifndef IRCBOT_TRANSPORT_HPP_
#define IRCBOT_TRANSPORT_HPP_

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/system/error_code.hpp>

namespace ircbot {
template <class T>
using IoResult = std::expected<T, boost::system::error_code>;
// A session exclusively owns its connected transport. All operations run on Run's executor.
// Close must wake a pending read, write or handshake. Async buffers remain valid until completion.
class Transport {
public:
    virtual ~Transport() = default;
    virtual boost::asio::any_io_executor Executor() = 0;
    virtual bool IsOpen() const = 0;
    virtual void Close() = 0;
    virtual boost::asio::awaitable<IoResult<void>> Handshake(const std::string& host,
                                                             const std::string& server_name) = 0;
    // A custom transport must explicitly support pinning when the configuration requests it.
    virtual bool MatchesPeerCertificate(std::string_view) {
        return false;
    }
    virtual long PeerVerificationResult() = 0;
    virtual boost::asio::awaitable<IoResult<std::size_t>>
    Read(boost::asio::mutable_buffer buffer) = 0;
    virtual boost::asio::awaitable<IoResult<void>> Write(boost::asio::const_buffer buffer) = 0;
    virtual boost::asio::awaitable<IoResult<void>> Shutdown() = 0;
};
} // namespace ircbot

#endif // IRCBOT_TRANSPORT_HPP_
