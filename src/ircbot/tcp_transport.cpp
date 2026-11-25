#include "tcp_transport.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/experimental/awaitable_operators.hpp>

#include "common/any_stream.hpp"
#include "common/connect.hpp"
#include "common/deadline.hpp"
#include "ircbot/client.hpp"

namespace ircbot::detail {
namespace {
namespace asio = boost::asio;
using asio::use_awaitable;
using Clock = std::chrono::steady_clock;
using tb::Deadline;

class SocketTransport final : public Transport {
public:
    SocketTransport(asio::ip::tcp::socket socket, asio::ssl::context* tls)
        : stream_(std::move(socket), tls) {}
    asio::any_io_executor Executor() override {
        return stream_.get_executor();
    }
    bool IsOpen() const override {
        return stream_.IsOpen();
    }
    void Close() override {
        boost::system::error_code ignored;
        stream_.Close(ignored);
    }
    asio::awaitable<IoResult<void>> Handshake(const std::string& host,
                                              const std::string& name) override {
        try {
            co_await stream_.Handshake(host, name);
            co_return IoResult<void>{};
        } catch (const boost::system::system_error& error) {
            co_return std::unexpected(error.code());
        }
    }
    bool MatchesPeerCertificate(std::string_view hex) override {
        return stream_.MatchesPeerCertificate(hex);
    }
    long PeerVerificationResult() override {
        return stream_.PeerVerificationResult();
    }
    asio::awaitable<IoResult<std::size_t>> Read(asio::mutable_buffer buffer) override {
        boost::system::error_code ec;
        auto count =
            co_await stream_.async_read_some(buffer, asio::redirect_error(use_awaitable, ec));
        if (ec) {
            co_return std::unexpected(ec);
        }
        co_return count;
    }
    asio::awaitable<IoResult<void>> Write(asio::const_buffer buffer) override {
        boost::system::error_code ec;
        co_await asio::async_write(stream_, buffer, asio::redirect_error(use_awaitable, ec));
        if (ec) {
            co_return std::unexpected(ec);
        }
        co_return IoResult<void>{};
    }
    asio::awaitable<IoResult<void>> Shutdown() override {
        try {
            co_await stream_.Shutdown();
            co_return IoResult<void>{};
        } catch (const boost::system::system_error& error) {
            co_return std::unexpected(error.code());
        }
    }

private:
    tb::AnyStream stream_;
};

using Tcp = asio::ip::tcp;

// A failed primary family releases the secondary immediately, even before it starts waiting.
asio::awaitable<Tcp::socket> ConnectFamily(std::vector<Tcp::endpoint> endpoints,
                                           Clock::time_point expires,
                                           std::shared_ptr<asio::steady_timer> stagger,
                                           bool secondary) {
    const auto executor = co_await asio::this_coro::executor;
    if (secondary && stagger->expiry() > Clock::now()) {
        boost::system::error_code ec;
        co_await stagger->async_wait(asio::redirect_error(use_awaitable, ec));
        const auto cancelled = co_await asio::this_coro::cancellation_state;
        if (cancelled.cancelled() != asio::cancellation_type::none) {
            throw boost::system::system_error(asio::error::operation_aborted);
        }
    }
    auto socket = std::make_shared<Tcp::socket>(executor);
    try {
        co_await tb::ConnectEndpoints(socket, endpoints, expires);
    } catch (...) {
        if (!secondary) {
            stagger->expires_at(Clock::now());
        }
        throw;
    }
    co_return std::move(*socket);
}

asio::awaitable<asio::ip::tcp::socket> Connect(const Config& config) {
    auto executor = co_await asio::this_coro::executor;
    auto resolver = std::make_shared<asio::ip::tcp::resolver>(executor);
    auto socket = std::make_shared<asio::ip::tcp::socket>(executor);
    const auto expires = tb::DeadlineAfter(Clock::now(), config.timeouts.connect_timeout);
    Deadline deadline(executor, expires);
    asio::co_spawn(
        executor,
        [resolver, socket, wait = deadline.Wait()]() mutable -> asio::awaitable<void> {
            if (co_await std::move(wait)) {
                resolver->cancel();
                boost::system::error_code ignored;
                socket->close(ignored);
            }
        },
        asio::detached);
    auto endpoints = co_await resolver->async_resolve(config.server, config.port, use_awaitable);
    if (endpoints.empty()) {
        throw boost::system::system_error(asio::error::host_not_found);
    }
    std::vector<Tcp::endpoint> primary, secondary;
    const auto family = endpoints.begin()->endpoint().protocol();
    for (const auto& candidate : endpoints) {
        const auto ep = candidate.endpoint();
        (ep.protocol() == family ? primary : secondary).push_back(ep);
    }
    if (secondary.empty()) {
        co_await tb::ConnectEndpoints(socket, primary, expires);
        co_return std::move(*socket);
    }
    using asio::experimental::awaitable_operators::operator||;
    auto stagger = std::make_shared<asio::steady_timer>(
        executor, std::min(expires, Clock::now() + std::chrono::milliseconds(250)));
    try {
        auto winner = co_await (ConnectFamily(std::move(primary), expires, stagger, false) ||
                                ConnectFamily(std::move(secondary), expires, stagger, true));
        if (winner.index() == 0) {
            co_return std::move(std::get<0>(winner));
        }
        co_return std::move(std::get<1>(winner));
    } catch (const asio::multiple_exceptions& errors) {
        std::rethrow_exception(errors.first_exception());
    }
}

} // namespace

boost::asio::awaitable<std::unique_ptr<Transport>> ConnectTcp(const Config& config,
                                                              boost::asio::ssl::context* tls) {
    auto socket = co_await Connect(config);
    co_return std::make_unique<SocketTransport>(std::move(socket), tls);
}
} // namespace ircbot::detail
