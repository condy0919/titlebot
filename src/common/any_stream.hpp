#ifndef TITLEBOT_ANY_STREAM_HPP_
#define TITLEBOT_ANY_STREAM_HPP_
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include <array>
#include <memory>
#include <string>
#include <string_view>
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
    /// Compare a leaf certificate with an expected 64-digit SHA-256 fingerprint after verification.
    bool MatchesPeerCertificate(std::string_view hex) {
        auto* tls = std::get_if<Tls>(&stream_);
        if (!tls || hex.size() != 64) {
            return false;
        }
        std::unique_ptr<X509, decltype(&X509_free)> cert(
            SSL_get_peer_certificate(tls->native_handle()), &X509_free);
        std::array<unsigned char, EVP_MAX_MD_SIZE> digest;
        unsigned int size = 0;
        if (!cert || !X509_digest(cert.get(), EVP_sha256(), digest.data(), &size) || size != 32) {
            return false;
        }
        auto nibble = [](char c) {
            if (c >= '0' && c <= '9') {
                return c - '0';
            }
            if (c >= 'a' && c <= 'f') {
                return c - 'a' + 10;
            }
            if (c >= 'A' && c <= 'F') {
                return c - 'A' + 10;
            }
            return -1;
        };
        for (std::size_t i = 0; i < size; ++i) {
            if (nibble(hex[2 * i]) != digest[i] / 16 || nibble(hex[2 * i + 1]) != digest[i] % 16) {
                return false;
            }
        }
        return true;
    }
    /// After a failed TLS handshake, report the OpenSSL peer verification result.
    /// For example, a self-signed peer may yield X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT.
    long PeerVerificationResult() {
        auto* tls = std::get_if<Tls>(&stream_);
        return tls ? SSL_get_verify_result(tls->native_handle()) : X509_V_OK;
    }
    bool IsOpen() const {
        if (const auto* plain = std::get_if<Socket>(&stream_)) {
            return plain->is_open();
        }
        return std::get<Tls>(stream_).next_layer().is_open();
    }
    void Close(boost::system::error_code& ec) {
        socket().close(ec);
    }
    /// Verify the peer certificate against host. A plain stream completes immediately.
    boost::asio::awaitable<void> Handshake(const std::string& host,
                                           const std::string& server_name = {}) {
        auto* tls = std::get_if<Tls>(&stream_);
        if (!tls) {
            co_return;
        }
        tls->set_verify_mode(boost::asio::ssl::verify_peer);
        boost::system::error_code ec;
        boost::asio::ip::make_address(host, ec);
        auto* params = SSL_get0_param(tls->native_handle());
        const int verified = ec ? X509_VERIFY_PARAM_set1_host(params, host.data(), host.size())
                                : X509_VERIFY_PARAM_set1_ip_asc(params, host.c_str());
        if (!verified) {
            throw boost::system::system_error(boost::asio::error::invalid_argument);
        }
        const auto& sni = server_name.empty() ? host : server_name;
        if ((!server_name.empty() || ec) &&
            !SSL_set_tlsext_host_name(tls->native_handle(), sni.c_str())) {
            throw boost::system::system_error(boost::asio::error::invalid_argument);
        }
        co_await tls->async_handshake(boost::asio::ssl::stream_base::client,
                                      boost::asio::use_awaitable);
    }
    /// For an established TLS stream, Shutdown() exchanges TLS close notifications.
    /// A plain TCP connection completes immediately.
    boost::asio::awaitable<void> Shutdown() {
        if (auto* tls = std::get_if<Tls>(&stream_)) {
            co_await tls->async_shutdown(boost::asio::use_awaitable);
        }
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
