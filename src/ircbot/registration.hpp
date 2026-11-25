#ifndef IRCBOT_REGISTRATION_HPP_
#define IRCBOT_REGISTRATION_HPP_

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "message.hpp"

namespace ircbot {
enum class SaslMechanism : std::uint8_t { kPlain, kExternal };

struct SaslConfig {
    SaslMechanism mechanism = SaslMechanism::kPlain;
    std::string username{};
    std::string password{};
    std::string authzid{};
    bool required = true;
};

struct RegistrationConfig {
    std::vector<std::string> caps = {"message-tags", "server-time"};
    std::optional<SaslConfig> sasl;
};

enum class RegistrationError : std::uint8_t {
    kInvalidConfig,
    kSaslUnavailable,
    kSaslRejected,
    kSaslFailed,
    kProtocol,
    kLimit,
};

/// Validate before connecting, limiting the unencoded SASL response to 12 KiB.
/// For example, a capability containing CRLF returns kInvalidConfig.
std::expected<void, RegistrationError> ValidateRegistration(const RegistrationConfig& config);

/// Negotiate one connection without performing I/O. For example, after Start(), handling
/// "CAP * LS :server-time" produces "CAP REQ :server-time". Output excludes CRLF.
/// The caller supplies NICK/USER, enforces deadlines and requires TLS for SASL PLAIN.
class Registration {
public:
    explicit Registration(RegistrationConfig config = {});

    /// Start once; returns {"CAP LS 302"}, or an empty vector on subsequent calls.
    std::vector<std::string> Start();

    /// Handle a parsed server message. For example, "903 bot :Success" completes an active
    /// SASL exchange, while "900 bot mask account :Logged in" does not complete it.
    std::expected<std::vector<std::string>, RegistrationError> Handle(const MessageView& message);

    // Negotiation completion is separate from receiving the server's 001 welcome.
    bool done() const {
        return done_;
    }
    bool authenticated() const {
        return authenticated_;
    }
    const std::unordered_set<std::string>& enabled() const {
        return enabled_;
    }

private:
    enum class SaslState : std::uint8_t { kIdle, kChallenge, kResult, kFinished };
    using Output = std::expected<std::vector<std::string>, RegistrationError>;

    Output Fail(RegistrationError error);
    Output SaslFailure(RegistrationError error, bool abort = false);
    Output HandleCap(const MessageView& message);
    Output RequestDesired();
    void MaybeFinish(std::vector<std::string>& output);
    bool MechanismAvailable() const;
    bool SaslActive() const;

    RegistrationConfig config_;
    std::unordered_map<std::string, std::string> available_;
    std::unordered_set<std::string> enabled_;
    std::unordered_set<std::string> pending_;
    std::unordered_set<std::string> rejected_;
    std::unordered_set<std::string> listing_;
    std::vector<std::string> acknowledging_;
    std::optional<RegistrationError> error_;
    SaslState sasl_state_ = SaslState::kIdle;
    bool started_ = false;
    bool done_ = false;
    bool authenticated_ = false;
    bool initial_ls_done_ = false;
    bool reading_ls_ = false;
    bool reading_list_ = false;
};
} // namespace ircbot

#endif // IRCBOT_REGISTRATION_HPP_
