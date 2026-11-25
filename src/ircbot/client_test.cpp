#include "ircbot/client.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

namespace {
namespace asio = boost::asio;
using asio::use_awaitable;
using asio::ip::tcp;
using namespace std::chrono_literals;

class Client : public testing::Test {
protected:
    void Exercise(asio::awaitable<void> task, std::chrono::seconds timeout = 4s) {
        auto result = asio::co_spawn(io, std::move(task), asio::use_future);
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (result.wait_for(0s) != std::future_status::ready && io.run_one_until(deadline)) {
        }
        ASSERT_EQ(result.wait_for(0s), std::future_status::ready);
        EXPECT_NO_THROW(result.get());
    }
    asio::io_context io;
};

template <class Stream>
asio::awaitable<std::string> Read(Stream& socket, std::string& input) {
    auto n = co_await asio::async_read_until(socket, asio::dynamic_buffer(input, 16384), "\r\n",
                                             use_awaitable);
    auto line = input.substr(0, n - 2);
    input.erase(0, n);
    co_return line;
}

template <class Stream>
asio::awaitable<void> Write(Stream& socket, std::string text) {
    co_await asio::async_write(socket, asio::buffer(text), use_awaitable);
}

ircbot::Config Configuration(tcp::acceptor& listener) {
    ircbot::Config config;
    config.server = "127.0.0.1";
    config.port = std::to_string(listener.local_endpoint().port());
    config.nick = "bot";
    config.channels = {"#room"};
    config.timeouts.reconnect_initial = 20ms;
    config.timeouts.reconnect_max = 40ms;
    return config;
}

asio::awaitable<void> Register(tcp::socket& peer, std::string& input, std::string nick = "bot") {
    EXPECT_EQ(co_await Read(peer, input), "CAP LS 302");
    EXPECT_EQ(co_await Read(peer, input), "NICK " + nick);
    EXPECT_EQ(co_await Read(peer, input), "USER " + nick + " 0 * :ircbot");
    co_await Write(peer, ":s CAP * LS :message-tags server-time\r\n");
    auto request = co_await Read(peer, input);
    EXPECT_TRUE(request.starts_with("CAP REQ :"));
    EXPECT_NE(request.find("message-tags"), request.npos);
    EXPECT_NE(request.find("server-time"), request.npos);
    co_await Write(peer, ":s CAP * ACK :message-tags server-time\r\n");
    EXPECT_EQ(co_await Read(peer, input), "CAP END");
    co_await Write(peer, ":s 001 " + nick + " :Welcome\r\n");
    EXPECT_EQ(co_await Read(peer, input), "JOIN #room");
}

TEST_F(Client, NegotiatesTagsDispatchesEventsAndCancelsCleanly) {
    tcp::acceptor listener(io, {asio::ip::address_v4::loopback(), 0});
    auto config = Configuration(listener);
    asio::cancellation_signal stop;
    ircbot::Connection saved;
    unsigned ready = 0, disconnected = 0;
    ircbot::Handlers handlers;
    handlers.ready = [&, marker = std::make_unique<bool>(true)](ircbot::Connection connection) {
        EXPECT_TRUE(*marker);
        saved = connection;
        ++ready;
    };
    handlers.disconnected = [&](ircbot::Connection connection) {
        ++disconnected;
        EXPECT_FALSE(connection.IsOpen());
        EXPECT_TRUE(connection.StopToken().stop_requested());
    };
    handlers.message = [&](ircbot::Connection connection, const ircbot::MessageView& message) {
        if (!ircbot::IsCommand(message, "PRIVMSG")) {
            return;
        }
        EXPECT_EQ(message.Tag("label"), "hello world");
        EXPECT_EQ(connection.casemapping(), ircbot::CaseMapping::kAscii);
        EXPECT_EQ(connection.Feature("CASEMAPPING"), "ascii");
        EXPECT_EQ(connection.NickLength(), 9u);
        EXPECT_FALSE(connection.IsSelf("bot{"));
        EXPECT_TRUE(connection.Privmsg(message.params[0], message.params[1]));
        EXPECT_EQ(connection.SendLine("@x=a; PRIVMSG #room :empty-tag").error(),
                  ircbot::SendError::kInvalidMessage);
        EXPECT_EQ(connection.SendLine("@x=a;x=b PRIVMSG #room :duplicate").error(),
                  ircbot::SendError::kInvalidMessage);
        EXPECT_EQ(connection.SendLine("PRIVMSG #room :injection\r\nQUIT").error(),
                  ircbot::SendError::kInvalidMessage);
    };
    unsigned routed = 0;
    handlers.subscriptions.push_back({"pRiVmSg", [&](auto, const auto&) { ++routed; }});
    auto finished = asio::co_spawn(io, ircbot::Run(config, handlers),
                                   asio::bind_cancellation_slot(stop.slot(), asio::use_future));
    auto exercise = [&]() -> asio::awaitable<void> {
        auto peer = co_await listener.async_accept(use_awaitable);
        std::string input;
        co_await Register(peer, input);
        EXPECT_TRUE(co_await saved.WaitWritable());
        co_await Write(peer, std::string(18000, 'x') + "\r\nPING :after-long-line\r\n");
        EXPECT_EQ(co_await Read(peer, input), "PONG :after-long-line");
        co_await Write(peer, ":s 005 bot CASEMAPPING=ascii :supported\r\n"
                             ":bot!u@h NICK :bot[\r\n"
                             "@label=hello\\sworld :bot{!u@h pRiVmSg #room :hello\r\n");
        EXPECT_EQ(co_await Read(peer, input), "PRIVMSG #room :hello");
        EXPECT_EQ(saved.Nick(), "bot[");
        std::thread worker([saved] {
            saved.PostPrivmsg("#room", "worker", [](auto result) { EXPECT_TRUE(result); });
        });
        worker.join();
        EXPECT_EQ(co_await Read(peer, input), "PRIVMSG #room :worker");
        co_await Write(peer, "PING " + std::string(505, 'a') + "\r\n");
        EXPECT_EQ(co_await Read(peer, input), "PONG " + std::string(505, 'a'));
        stop.emit(asio::cancellation_type::terminal);
        EXPECT_EQ(co_await Read(peer, input), "QUIT :Shutting down");
    };
    Exercise(exercise());
    io.run_for(100ms);
    ASSERT_EQ(finished.wait_for(0s), std::future_status::ready);
    EXPECT_TRUE(finished.get());
    EXPECT_EQ(ready, 1u);
    EXPECT_EQ(routed, 1u);
    EXPECT_EQ(disconnected, 1u);
    EXPECT_FALSE(saved.IsOpen());
}

TEST_F(Client, WaitsForSpaceInPacedQueueWithoutDroppingPendingMessages) {
    tcp::acceptor listener(io, {asio::ip::address_v4::loopback(), 0});
    auto config = Configuration(listener);
    config.queued_lines = 4;
    config.send_interval = 20ms;
    asio::cancellation_signal stop;
    ircbot::Connection connection;
    ircbot::Handlers handlers;
    handlers.ready = [&](auto active) { connection = active; };
    auto finished = asio::co_spawn(io, ircbot::Run(config, handlers),
                                   asio::bind_cancellation_slot(stop.slot(), asio::use_future));
    auto exercise = [&]() -> asio::awaitable<void> {
        auto peer = co_await listener.async_accept(use_awaitable);
        std::string input;
        co_await Register(peer, input);
        for (const auto text : {"one", "two", "three", "four"}) {
            EXPECT_TRUE(connection.Privmsg("#room", text));
        }
        EXPECT_EQ(connection.Privmsg("#room", "five"), std::unexpected(ircbot::SendError::kQueueFull));
        EXPECT_TRUE(co_await connection.WaitWritable());
        EXPECT_TRUE(connection.Privmsg("#room", "five"));
        for (const auto text : {"one", "two", "three", "four", "five"}) {
            EXPECT_EQ(co_await Read(peer, input), std::string("PRIVMSG #room :") + text);
        }
        stop.emit(asio::cancellation_type::terminal);
        EXPECT_EQ(co_await Read(peer, input), "QUIT :Shutting down");
    };
    Exercise(exercise());
    io.run();
    EXPECT_TRUE(finished.get());
}

TEST_F(Client, OldConnectionCannotSendAfterReconnect) {
    tcp::acceptor listener(io, {asio::ip::address_v4::loopback(), 0});
    asio::cancellation_signal stop;
    std::vector<ircbot::Connection> connections;
    ircbot::Handlers handlers;
    handlers.ready = [&](auto connection) { connections.push_back(connection); };
    auto finished = asio::co_spawn(io, ircbot::Run(Configuration(listener), handlers),
                                   asio::bind_cancellation_slot(stop.slot(), asio::use_future));
    auto exercise = [&]() -> asio::awaitable<void> {
        for (int generation = 0; generation < 2; ++generation) {
            auto peer = co_await listener.async_accept(use_awaitable);
            std::string input;
            co_await Register(peer, input);
            if (generation) {
                EXPECT_EQ(connections.size(), 2u);
                EXPECT_EQ(connections[0].Privmsg("#room", "stale").error(),
                          ircbot::SendError::kClosed);
                EXPECT_TRUE(connections[1].Privmsg("#room", "current"));
                EXPECT_EQ(co_await Read(peer, input), "PRIVMSG #room :current");
                stop.emit(asio::cancellation_type::terminal);
                EXPECT_EQ(co_await Read(peer, input), "QUIT :Shutting down");
            }
        }
    };
    Exercise(exercise());
    io.run_for(100ms);
    ASSERT_EQ(finished.wait_for(0s), std::future_status::ready);
    EXPECT_TRUE(finished.get());
}

TEST_F(Client, RunsIndependentClientsOnOneExecutor) {
    tcp::acceptor one(io, {asio::ip::address_v4::loopback(), 0});
    tcp::acceptor two(io, {asio::ip::address_v4::loopback(), 0});
    asio::cancellation_signal stop_one, stop_two;
    auto first = asio::co_spawn(io, ircbot::Run(Configuration(one)),
                                asio::bind_cancellation_slot(stop_one.slot(), asio::use_future));
    auto second = asio::co_spawn(io, ircbot::Run(Configuration(two)),
                                 asio::bind_cancellation_slot(stop_two.slot(), asio::use_future));
    auto exercise = [&]() -> asio::awaitable<void> {
        auto a = co_await one.async_accept(use_awaitable);
        auto b = co_await two.async_accept(use_awaitable);
        std::string a_input, b_input;
        co_await Register(a, a_input);
        co_await Register(b, b_input);
        stop_one.emit(asio::cancellation_type::terminal);
        EXPECT_EQ(co_await Read(a, a_input), "QUIT :Shutting down");
        co_await Write(b, "PING :still-running\r\n");
        EXPECT_EQ(co_await Read(b, b_input), "PONG :still-running");
        stop_two.emit(asio::cancellation_type::terminal);
        EXPECT_EQ(co_await Read(b, b_input), "QUIT :Shutting down");
    };
    Exercise(exercise());
    io.run_for(100ms);
    ASSERT_EQ(first.wait_for(0s), std::future_status::ready);
    ASSERT_EQ(second.wait_for(0s), std::future_status::ready);
    EXPECT_TRUE(first.get());
    EXPECT_TRUE(second.get());
}

TEST_F(Client, RequiredSaslDoesNotSilentlyJoinUnauthenticated) {
    tcp::acceptor listener(io, {asio::ip::address_v4::loopback(), 0});
    auto config = Configuration(listener);
    config.registration.sasl = ircbot::SaslConfig{.mechanism = ircbot::SaslMechanism::kExternal,
                                                  .username = {},
                                                  .password = {},
                                                  .authzid = {}};
    bool ready = false;
    ircbot::Handlers handlers;
    std::optional<ircbot::RegistrationError> negotiation_error;
    handlers.diagnostic_event = [&](const ircbot::DiagnosticEvent& event) {
        if (event.kind == ircbot::DiagnosticKind::kRegistration) {
            negotiation_error = event.registration;
        }
    };
    handlers.ready = [&](auto) { ready = true; };
    auto finished = asio::co_spawn(io, ircbot::Run(config, handlers), asio::use_future);
    auto exercise = [&]() -> asio::awaitable<void> {
        auto peer = co_await listener.async_accept(use_awaitable);
        std::string input;
        for (unsigned i = 0; i < 3; ++i) {
            co_await Read(peer, input);
        }
        co_await Write(peer, ":s CAP * LS :server-time\r\n");
        char byte;
        boost::system::error_code ec;
        co_await peer.async_read_some(asio::buffer(&byte, 1),
                                      asio::redirect_error(use_awaitable, ec));
        EXPECT_EQ(ec, asio::error::eof);
    };
    Exercise(exercise());
    io.run_for(100ms);
    ASSERT_EQ(finished.wait_for(0s), std::future_status::ready);
    EXPECT_EQ(finished.get().error(), ircbot::ClientError::kAuthentication);
    EXPECT_EQ(negotiation_error, ircbot::RegistrationError::kSaslUnavailable);
    EXPECT_FALSE(ready);
}

TEST_F(Client, RejectsPlainAuthenticationWithoutTlsBeforeConnecting) {
    ircbot::Config config;
    config.server = "127.0.0.1";
    config.nick = "bot";
    config.registration.sasl =
        ircbot::SaslConfig{.username = "bot", .password = "secret", .authzid = {}};
    std::string diagnostic;
    ircbot::Handlers handlers;
    handlers.diagnostic_event = [&](const auto& event) {
        if (event.kind == ircbot::DiagnosticKind::kConfiguration) {
            diagnostic = event.message;
        }
    };
    auto result = asio::co_spawn(io, ircbot::Run(config, handlers), asio::use_future);
    io.run();
    EXPECT_EQ(result.get().error(), ircbot::ClientError::kInvalidConfig);
    EXPECT_NE(diagnostic.find("registration.sasl/tls"), diagnostic.npos);
}

TEST_F(Client, ReturnsHandlerFailureAndClosesConnection) {
    tcp::acceptor listener(io, {asio::ip::address_v4::loopback(), 0});
    ircbot::Handlers handlers;
    bool completed = false;
    std::exception_ptr original_exception;
    handlers.diagnostic_event = [&](const ircbot::DiagnosticEvent& event) {
        if (event.kind == ircbot::DiagnosticKind::kCallback) {
            original_exception = event.exception;
        }
    };
    handlers.diagnostic = [&](std::string_view) { EXPECT_FALSE(completed); };
    handlers.connected = [](auto) { throw std::runtime_error("handler failure"); };
    std::promise<std::expected<void, ircbot::ClientError>> completion;
    auto result = completion.get_future();
    asio::co_spawn(io, ircbot::Run(Configuration(listener), handlers),
                   [&](std::exception_ptr error, std::expected<void, ircbot::ClientError> outcome) {
                       EXPECT_FALSE(error);
                       completed = true;
                       completion.set_value(outcome);
                   });
    auto exercise = [&]() -> asio::awaitable<void> {
        auto peer = co_await listener.async_accept(use_awaitable);
        char bytes[1024];
        boost::system::error_code ec;
        while (!ec) {
            co_await peer.async_read_some(asio::buffer(bytes),
                                          asio::redirect_error(use_awaitable, ec));
        }
    };
    Exercise(exercise());
    io.run_for(100ms);
    ASSERT_EQ(result.wait_for(0s), std::future_status::ready);
    EXPECT_EQ(result.get().error(), ircbot::ClientError::kHandler);
    EXPECT_TRUE(original_exception);
    completed = true;
    io.restart();
    io.run_for(20ms);
}

TEST_F(Client, TimesOutBlockedWritesEvenWhenPeerKeepsSending) {
    tcp::acceptor listener(io, {asio::ip::address_v4::loopback(), 0});
    auto config = Configuration(listener);
    config.queued_lines = 16000;
    config.timeouts.write_timeout = 30ms;
    config.timeouts.reconnect_initial = 1s;
    asio::cancellation_signal stop;
    bool write_timeout = false;
    ircbot::Handlers handlers;
    handlers.ready = [](ircbot::Connection connection) {
        for (unsigned i = 0; i < 12000; ++i) {
            EXPECT_TRUE(connection.Privmsg("#room", std::string(480, 'x')));
        }
    };
    handlers.diagnostic = [&](std::string_view message) {
        write_timeout |= message == "IRC write timed out";
    };
    auto result = asio::co_spawn(io, ircbot::Run(config, handlers),
                                 asio::bind_cancellation_slot(stop.slot(), asio::use_future));
    auto exercise = [&]() -> asio::awaitable<void> {
        auto peer = co_await listener.async_accept(use_awaitable);
        peer.set_option(asio::socket_base::receive_buffer_size(1024));
        std::string input;
        // The peer stops reading immediately after 001, while continuing to send valid input.
        for (unsigned i = 0; i < 3; ++i) {
            co_await Read(peer, input);
        }
        co_await Write(peer, ":s CAP * LS :\r\n");
        EXPECT_EQ(co_await Read(peer, input), "CAP END");
        co_await Write(peer, ":s 001 bot :Welcome\r\n");
        asio::steady_timer tick(io);
        for (unsigned i = 0; i < 400 && !write_timeout; ++i) {
            tick.expires_after(10ms);
            co_await tick.async_wait(use_awaitable);
            boost::system::error_code ec;
            co_await asio::async_write(peer, asio::buffer(std::string_view("PONG :alive\r\n")),
                                       asio::redirect_error(use_awaitable, ec));
            if (ec) {
                break;
            }
        }
        EXPECT_TRUE(write_timeout);
        stop.emit(asio::cancellation_type::terminal);
    };
    Exercise(exercise(), 8s);
    io.run_for(100ms);
    ASSERT_EQ(result.wait_for(0s), std::future_status::ready);
    EXPECT_TRUE(result.get());
}
TEST_F(Client, AcceptsIpv6ServerLiteralsAndRetriesShortUnicodeNicknames) {
    tcp::acceptor listener(io, {asio::ip::address_v6::loopback(), 0});
    auto config = Configuration(listener);
    config.server = "::1";
    config.nick = "中文";
    asio::cancellation_signal stop;
    auto result = asio::co_spawn(io, ircbot::Run(config),
                                 asio::bind_cancellation_slot(stop.slot(), asio::use_future));
    auto exercise = [&]() -> asio::awaitable<void> {
        auto peer = co_await listener.async_accept(use_awaitable);
        std::string input;
        for (unsigned i = 0; i < 3; ++i) {
            co_await Read(peer, input);
        }
        co_await Write(peer, ":s CAP * LS :\r\n:s 005 * NICKLEN=2 :supported\r\n"
                             ":s 433 * 中文 :In use\r\n");
        EXPECT_EQ(co_await Read(peer, input), "CAP END");
        auto first = co_await Read(peer, input);
        EXPECT_EQ(first.size(), 7u);
        EXPECT_TRUE(first.starts_with("NICK _"));
        co_await Write(peer, ":s 437 * " + first.substr(5) + " :In use\r\n");
        auto second = co_await Read(peer, input);
        EXPECT_EQ(second.size(), 7u);
        EXPECT_TRUE(second.starts_with("NICK _"));
        EXPECT_NE(first, second);
        stop.emit(asio::cancellation_type::terminal);
        EXPECT_EQ(co_await Read(peer, input), "QUIT :Shutting down");
    };
    Exercise(exercise());
    io.run_for(100ms);
    ASSERT_EQ(result.wait_for(0s), std::future_status::ready);
    EXPECT_TRUE(result.get());
}

TEST_F(Client, ConnectsToFallbackAfterPrimaryDisconnects) {
    tcp::acceptor primary(io, {asio::ip::address_v4::loopback(), 0});
    tcp::acceptor fallback(io, {asio::ip::address_v4::loopback(), 0});
    auto config = Configuration(primary);
    config.fallback_servers.push_back({"127.0.0.1", std::to_string(fallback.local_endpoint().port())});
    asio::cancellation_signal stop;
    auto result = asio::co_spawn(io, ircbot::Run(config),
                                 asio::bind_cancellation_slot(stop.slot(), asio::use_future));
    auto exercise = [&]() -> asio::awaitable<void> {
        {
            auto peer = co_await primary.async_accept(use_awaitable);
            std::string input;
            co_await Register(peer, input);
        }
        auto peer = co_await fallback.async_accept(use_awaitable);
        std::string input;
        co_await Register(peer, input);
        stop.emit(asio::cancellation_type::terminal);
        EXPECT_EQ(co_await Read(peer, input), "QUIT :Shutting down");
    };
    Exercise(exercise());
    io.run();
    EXPECT_TRUE(result.get());
}

TEST_F(Client, ReportsInvalidNicknameBeforeRegistrationTimesOut) {
    tcp::acceptor listener(io, {asio::ip::address_v4::loopback(), 0});
    auto result = asio::co_spawn(io, ircbot::Run(Configuration(listener)), asio::use_future);
    auto exercise = [&]() -> asio::awaitable<void> {
        auto peer = co_await listener.async_accept(use_awaitable);
        std::string input;
        for (int i = 0; i < 3; ++i) {
            co_await Read(peer, input);
        }
        co_await Write(peer, ":irc 432 * bot :Erroneous nickname\r\n");
        boost::system::error_code ec;
        char byte;
        co_await peer.async_read_some(asio::buffer(&byte, 1), asio::redirect_error(use_awaitable, ec));
        EXPECT_EQ(ec, asio::error::eof);
    };
    Exercise(exercise());
    io.run_for(100ms);
    ASSERT_EQ(result.wait_for(0s), std::future_status::ready);
    EXPECT_EQ(result.get().error(), ircbot::ClientError::kInvalidNickname);
}

TEST_F(Client, LetsPolicyTerminateTransientConnectionFailures) {
    tcp::acceptor listener(io, {asio::ip::address_v4::loopback(), 0});
    auto config = Configuration(listener);
    std::vector<ircbot::ClientError> failures;
    config.should_retry = [&](auto error, std::size_t retries) {
        EXPECT_EQ(retries, 0u);
        failures.push_back(error);
        return false;
    };
    auto result = asio::co_spawn(io, ircbot::Run(config), asio::use_future);
    auto exercise = [&]() -> asio::awaitable<void> {
        auto peer = co_await listener.async_accept(use_awaitable);
        std::string input;
        co_await Register(peer, input);
        peer.close();
    };
    Exercise(exercise());
    io.run();
    EXPECT_EQ(result.get().error(), ircbot::ClientError::kNetwork);
    EXPECT_EQ(failures, (std::vector{ircbot::ClientError::kNetwork}));
}

class ScriptedTransport final : public ircbot::Transport {
public:
    ScriptedTransport(asio::any_io_executor executor, std::string& transcript, bool reject = true)
        : gate_(executor, std::chrono::steady_clock::time_point::max()), transcript_(transcript), reject_(reject) {}
    asio::any_io_executor Executor() override { return gate_.get_executor(); }
    bool IsOpen() const override { return open_; }
    void Close() override {
        open_ = false;
        gate_.cancel();
    }
    asio::awaitable<ircbot::IoResult<void>> Handshake(const std::string&, const std::string&) override { co_return ircbot::IoResult<void>{}; }
    long PeerVerificationResult() override { return X509_V_OK; }
    asio::awaitable<ircbot::IoResult<void>> Shutdown() override { co_return ircbot::IoResult<void>{}; }
    asio::awaitable<ircbot::IoResult<std::size_t>> Read(asio::mutable_buffer buffer) override {
        if (!reject_) {
            while (open_) {
                boost::system::error_code ec;
                co_await gate_.async_wait(asio::redirect_error(use_awaitable, ec));
            }
            co_return std::unexpected(boost::system::error_code(asio::error::eof));
        }
        // Wait for NICK to be written before returning the server's rejection.
        if (transcript_.find("NICK bot\r\n") == transcript_.npos) {
            boost::system::error_code ec;
            co_await gate_.async_wait(asio::redirect_error(use_awaitable, ec));
        }
        constexpr std::string_view rejection = ":s 432 * bot :bad nickname\r\n";
        co_return asio::buffer_copy(buffer, asio::buffer(rejection));
    }
    asio::awaitable<ircbot::IoResult<void>> Write(asio::const_buffer buffer) override {
        transcript_.append(static_cast<const char*>(buffer.data()), buffer.size());
        gate_.cancel();
        co_return ircbot::IoResult<void>{};
    }
private:
    asio::steady_timer gate_;
    std::string& transcript_;
    bool open_ = true;
    bool reject_;
};

TEST_F(Client, AdvancesRetryBackoffWithoutSocketOrWallClockWaits) {
    ircbot::Config config;
    config.server = "example.invalid";
    config.nick = "bot";
    config.clock = std::make_shared<ircbot::ManualClock>();
    config.reconnect_jitter = false;
    config.max_retries = 1;
    unsigned attempts = 0;
    ircbot::Connector connect = [&](const auto&) -> asio::awaitable<ircbot::IoResult<std::unique_ptr<ircbot::Transport>>> {
        ++attempts;
        co_return std::unexpected(boost::system::error_code(asio::error::host_unreachable));
    };
    auto result = asio::co_spawn(io, ircbot::Run(config, {}, connect), asio::use_future);
    io.poll();
    ASSERT_EQ(attempts, 1u);
    EXPECT_FALSE(config.clock->Advance(-1ms));
    ASSERT_TRUE(config.clock->Advance(config.timeouts.reconnect_initial - 1ms));
    io.poll();
    EXPECT_EQ(attempts, 1u);
    ASSERT_TRUE(config.clock->Advance(1ms));
    io.poll();
    ASSERT_EQ(result.wait_for(0s), std::future_status::ready);
    EXPECT_EQ(result.get().error(), ircbot::ClientError::kRetryLimit);
    EXPECT_EQ(attempts, 2u);
}

TEST_F(Client, AdvancesRegistrationDeadlineWithoutWallClockWaits) {
    std::string transcript;
    ircbot::Config config;
    config.server = "example.invalid";
    config.nick = "bot";
    config.clock = std::make_shared<ircbot::ManualClock>();
    config.max_retries = 0;
    ircbot::Connector connector = [&](const auto&) -> asio::awaitable<ircbot::IoResult<std::unique_ptr<ircbot::Transport>>> {
        co_return std::make_unique<ScriptedTransport>(co_await asio::this_coro::executor, transcript, false);
    };
    auto result = asio::co_spawn(io, ircbot::Run(config, {}, connector), asio::use_future);
    io.poll();
    ASSERT_NE(transcript.find("NICK bot"), transcript.npos);
    EXPECT_NE(result.wait_for(0s), std::future_status::ready);
    ASSERT_TRUE(config.clock->Advance(config.timeouts.registration_timeout));
    io.poll();
    ASSERT_EQ(result.wait_for(0s), std::future_status::ready);
    EXPECT_EQ(result.get().error(), ircbot::ClientError::kRetryLimit);
}

TEST_F(Client, ExercisesRegistrationWithoutSockets) {
    std::string transcript;
    ircbot::Connector connect = [&](const auto&) -> asio::awaitable<ircbot::IoResult<std::unique_ptr<ircbot::Transport>>> {
        co_return std::make_unique<ScriptedTransport>(co_await asio::this_coro::executor, transcript);
    };
    ircbot::Config config;
    config.server = "test.invalid";
    config.nick = "bot";
    auto result = asio::co_spawn(io, ircbot::Run(config, {}, connect), asio::use_future);
    io.run();
    ASSERT_EQ(result.get().error(), ircbot::ClientError::kInvalidNickname);
    EXPECT_NE(transcript.find("NICK bot\r\n"), transcript.npos);
}

TEST_F(Client, RestoresDynamicKeyedChannelAfterReconnect) {
    tcp::acceptor listener(io, {asio::ip::address_v4::loopback(), 0});
    auto config = Configuration(listener);
    ircbot::Connection connection;
    ircbot::Handlers handlers;
    handlers.ready = [&](auto handle) { connection = handle; };
    asio::cancellation_signal stop;
    auto result = asio::co_spawn(io, ircbot::Run(config, handlers),
                                 asio::bind_cancellation_slot(stop.slot(), asio::use_future));
    auto exercise = [&]() -> asio::awaitable<void> {
        for (int attempt = 0; attempt < 2; ++attempt) {
            auto peer = co_await listener.async_accept(use_awaitable);
            std::string input;
            co_await Register(peer, input);
            if (attempt == 0) {
                EXPECT_TRUE(connection.Join("#private", "secret"));
            }
            EXPECT_EQ(co_await Read(peer, input), "JOIN #private secret");
            if (attempt == 1) {
                EXPECT_TRUE(connection.Part("#private"));
                EXPECT_EQ(co_await Read(peer, input), "PART #private");
                stop.emit(asio::cancellation_type::terminal);
                EXPECT_EQ(co_await Read(peer, input), "QUIT :Shutting down");
            }
        }
    };
    Exercise(exercise());
    io.run();
    EXPECT_TRUE(result.get());
}

TEST_F(Client, AcquiresNewCredentialsOnReconnect) {
    tcp::acceptor listener(io, {asio::ip::address_v4::loopback(), 0});
    auto config = Configuration(listener);
    int attempts = 0;
    config.sasl_credentials = [&] {
        ircbot::SaslConfig credentials;
        credentials.mechanism = ircbot::SaslMechanism::kExternal;
        credentials.authzid = ++attempts == 1 ? "a" : "b";
        return std::optional{std::move(credentials)};
    };
    asio::cancellation_signal stop;
    auto result = asio::co_spawn(io, ircbot::Run(config),
                                 asio::bind_cancellation_slot(stop.slot(), asio::use_future));
    auto exercise = [&]() -> asio::awaitable<void> {
        for (int attempt = 0; attempt < 2; ++attempt) {
            auto peer = co_await listener.async_accept(use_awaitable);
            std::string input;
            for (int i = 0; i < 3; ++i) { co_await Read(peer, input); }
            co_await Write(peer, ":s CAP * LS :sasl=EXTERNAL\r\n");
            EXPECT_EQ(co_await Read(peer, input), "CAP REQ :sasl");
            co_await Write(peer, ":s CAP bot ACK :sasl\r\n");
            EXPECT_EQ(co_await Read(peer, input), "AUTHENTICATE EXTERNAL");
            co_await Write(peer, "AUTHENTICATE +\r\n");
            EXPECT_EQ(co_await Read(peer, input), attempt == 0 ? "AUTHENTICATE YQ==" : "AUTHENTICATE Yg==");
            co_await Write(peer, ":s 903 bot :success\r\n:s 001 bot :welcome\r\n");
            EXPECT_EQ(co_await Read(peer, input), "CAP END");
            EXPECT_EQ(co_await Read(peer, input), "JOIN #room");
            if (attempt == 1) {
                stop.emit(asio::cancellation_type::terminal);
                EXPECT_EQ(co_await Read(peer, input), "QUIT :Shutting down");
            }
        }
    };
    Exercise(exercise());
    io.run();
    EXPECT_EQ(attempts, 2);
    EXPECT_TRUE(result.get());
}

TEST_F(Client, ZeroRetryBudgetEndsAfterFirstDisconnect) {
    tcp::acceptor listener(io, {asio::ip::address_v4::loopback(), 0});
    auto config = Configuration(listener);
    config.max_retries = 0;
    auto result = asio::co_spawn(io, ircbot::Run(config), asio::use_future);
    auto exercise = [&]() -> asio::awaitable<void> {
        auto peer = co_await listener.async_accept(use_awaitable);
        std::string input;
        co_await Register(peer, input);
        peer.close();
    };
    Exercise(exercise());
    io.run_for(100ms);
    ASSERT_EQ(result.wait_for(0s), std::future_status::ready);
    EXPECT_EQ(result.get().error(), ircbot::ClientError::kRetryLimit);
}

TEST_F(Client, ReportsHostnameMismatchEvenWhenIssuerIsTrusted) {
    tcp::acceptor listener(io, {asio::ip::address_v4::loopback(), 0});
    asio::ssl::context tls(asio::ssl::context::tls_server);
    const auto cert = IRCBOT_TEST_ROOT "/tests/fixtures/localhost-test.pem";
    tls.use_certificate_chain_file(cert);
    tls.use_private_key_file(cert, asio::ssl::context::pem);
    auto config = Configuration(listener);
    config.tls = true;
    config.ca_file = cert;
    config.tls_server_name = "localhost";
    ircbot::Handlers handlers;
    std::optional<long> reason;
    handlers.diagnostic_event = [&](const ircbot::DiagnosticEvent& event) {
        if (event.certificate_error) { reason = event.certificate_error; }
    };
    auto result = asio::co_spawn(io, ircbot::Run(config, handlers), asio::use_future);
    auto exercise = [&]() -> asio::awaitable<void> {
        asio::ssl::stream<tcp::socket> peer(co_await listener.async_accept(use_awaitable), tls);
        boost::system::error_code ec;
        co_await peer.async_handshake(asio::ssl::stream_base::server, asio::redirect_error(use_awaitable, ec));
        EXPECT_TRUE(ec);
    };
    Exercise(exercise());
    io.run_for(100ms);
    ASSERT_EQ(result.wait_for(0s), std::future_status::ready);
    EXPECT_EQ(result.get().error(), ircbot::ClientError::kTlsTrust);
    EXPECT_EQ(reason, X509_V_ERR_IP_ADDRESS_MISMATCH);
}

TEST_F(Client, UntrustedCertificateStopsReconnects) {
    tcp::acceptor listener(io, {asio::ip::address_v4::loopback(), 0});
    asio::ssl::context tls(asio::ssl::context::tls_server);
    const auto cert = IRCBOT_TEST_ROOT "/tests/fixtures/localhost-test.pem";
    tls.use_certificate_chain_file(cert);
    tls.use_private_key_file(cert, asio::ssl::context::pem);
    auto config = Configuration(listener);
    config.tls = true;
    auto result = asio::co_spawn(io, ircbot::Run(config), asio::use_future);
    auto exercise = [&]() -> asio::awaitable<void> {
        asio::ssl::stream<tcp::socket> peer(co_await listener.async_accept(use_awaitable), tls);
        boost::system::error_code ec;
        co_await peer.async_handshake(asio::ssl::stream_base::server,
                                      asio::redirect_error(use_awaitable, ec));
        EXPECT_TRUE(ec);
    };
    Exercise(exercise());
    io.run_for(100ms);
    ASSERT_EQ(result.wait_for(0s), std::future_status::ready);
    EXPECT_EQ(result.get().error(), ircbot::ClientError::kTlsTrust);
}

TEST_F(Client, CompletesExternalAuthenticationOverVerifiedMutualTls) {
    tcp::acceptor listener(io, {asio::ip::address_v4::loopback(), 0});
    const std::string certificate = IRCBOT_TEST_ROOT "/tests/fixtures/localhost-test.pem";
    asio::ssl::context tls(asio::ssl::context::tls_server);
    tls.use_certificate_chain_file(certificate);
    tls.use_private_key_file(certificate, asio::ssl::context::pem);
    tls.load_verify_file(certificate);
    tls.set_verify_mode(asio::ssl::verify_peer | asio::ssl::verify_fail_if_no_peer_cert);
    auto config = Configuration(listener);
    config.server = "localhost";
    config.tls = true;
    config.channel_keys["#room"] = "secret";
    config.ca_file = certificate;
    config.certificate_file = certificate;
    config.private_key_file = certificate;
    config.registration.sasl = ircbot::SaslConfig{.mechanism = ircbot::SaslMechanism::kExternal};
    asio::cancellation_signal stop;
    auto result = asio::co_spawn(io, ircbot::Run(config),
                                 asio::bind_cancellation_slot(stop.slot(), asio::use_future));
    auto exercise = [&]() -> asio::awaitable<void> {
        asio::ssl::stream<tcp::socket> peer(co_await listener.async_accept(use_awaitable), tls);
        co_await peer.async_handshake(asio::ssl::stream_base::server, use_awaitable);
        std::string input;
        for (unsigned i = 0; i < 3; ++i) {
            co_await Read(peer, input);
        }
        co_await Write(peer, ":s CAP * LS :sasl=EXTERNAL\r\n");
        EXPECT_EQ(co_await Read(peer, input), "CAP REQ :sasl");
        co_await Write(peer, ":s CAP * ACK :sasl\r\n");
        EXPECT_EQ(co_await Read(peer, input), "AUTHENTICATE EXTERNAL");
        co_await Write(peer, "PING :during-sasl\r\n");
        EXPECT_EQ(co_await Read(peer, input), "PONG :during-sasl");
        co_await Write(peer, "AUTHENTICATE +\r\n");
        EXPECT_EQ(co_await Read(peer, input), "AUTHENTICATE +");
        co_await Write(peer, ":s 903 bot :Success\r\n");
        EXPECT_EQ(co_await Read(peer, input), "CAP END");
        co_await Write(peer, ":s 001 bot :Welcome\r\n");
        EXPECT_EQ(co_await Read(peer, input), "JOIN #room secret");
        stop.emit(asio::cancellation_type::terminal);
        EXPECT_EQ(co_await Read(peer, input), "QUIT :Shutting down");
    };
    Exercise(exercise());
    io.run_for(100ms);
    ASSERT_EQ(result.wait_for(0s), std::future_status::ready);
    EXPECT_TRUE(result.get());
}
TEST_F(Client, CancellationFromDisconnectCallbackCompletesSuccessfully) {
    tcp::acceptor listener(io, {asio::ip::address_v4::loopback(), 0});
    asio::cancellation_signal stop;
    ircbot::Handlers handlers;
    handlers.disconnected = [&](auto) { stop.emit(asio::cancellation_type::terminal); };
    auto result = asio::co_spawn(io, ircbot::Run(Configuration(listener), handlers),
                                 asio::bind_cancellation_slot(stop.slot(), asio::use_future));
    auto exercise = [&]() -> asio::awaitable<void> {
        auto peer = co_await listener.async_accept(use_awaitable);
        std::string input;
        co_await Register(peer, input);
        peer.close();
    };
    Exercise(exercise());
    io.run_for(100ms);
    ASSERT_EQ(result.wait_for(0s), std::future_status::ready);
    EXPECT_TRUE(result.get());
}
} // namespace
