#ifndef TITLEBOT_ANY_STREAM_HPP_
#define TITLEBOT_ANY_STREAM_HPP_
#include <string>
#include <utility>
#include <variant>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
namespace tb {
class AnyStream {
    using Socket = boost::asio::ip::tcp::socket;
    using Tls = boost::asio::ssl::stream<Socket>;
    using Transport = std::variant<Socket, Tls>;

public:
    using executor_type = Socket::executor_type;
    AnyStream(Socket socket, boost::asio::ssl::context* tls)
        : stream_(tls ? Transport(std::in_place_type<Tls>, std::move(socket), *tls)
                      : Transport(std::in_place_type<Socket>, std::move(socket))) {}
    Socket& socket() {
        if (auto* plain = std::get_if<Socket>(&stream_)) {
            return *plain;
        }
        return std::get<Tls>(stream_).next_layer();
    }
    executor_type get_executor() {
        return socket().get_executor();
    }
    bool IsOpen() {
        return socket().is_open();
    }
    void Close(boost::system::error_code& ec) {
        socket().close(ec);
    }
    /// Verify the peer certificate against host. A plain stream completes immediately.
    boost::asio::awaitable<void> Handshake(const std::string& host) {
        auto* tls = std::get_if<Tls>(&stream_);
        if (!tls) {
            co_return;
        }
        tls->set_verify_mode(boost::asio::ssl::verify_peer);
        tls->set_verify_callback(boost::asio::ssl::host_name_verification(host));
        boost::system::error_code ec;
        boost::asio::ip::make_address(host, ec);
        if (ec && !SSL_set_tlsext_host_name(tls->native_handle(), host.c_str())) {
            throw boost::system::system_error(boost::asio::error::invalid_argument);
        }
        co_await tls->async_handshake(boost::asio::ssl::stream_base::client,
                                      boost::asio::use_awaitable);
    }
    template <class Buffers, class Token>
    auto async_read_some(const Buffers& buffers, Token&& token) {
        return std::visit(
            [&](auto& stream) {
                return stream.async_read_some(buffers, std::forward<Token>(token));
            },
            stream_);
    }
    template <class Buffers, class Token>
    auto async_write_some(const Buffers& buffers, Token&& token) {
        return std::visit(
            [&](auto& stream) {
                return stream.async_write_some(buffers, std::forward<Token>(token));
            },
            stream_);
    }

private:
    Transport stream_;
};
} // namespace tb
#endif // TITLEBOT_ANY_STREAM_HPP_
