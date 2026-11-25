#include "ircbot/registration.hpp"

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <utility>

namespace ircbot {
namespace {
constexpr std::size_t kMaxCapabilities = 512;
constexpr std::size_t kRequestBytes = 400;
constexpr std::size_t kMaxCredentialBytes = 12 * 1024;

// Capability names are opaque and case-sensitive; reject delimiters and wire controls.
bool ValidCapability(std::string_view name) {
    return !name.empty() && name.size() <= kRequestBytes && name.front() != '-' &&
           std::ranges::all_of(name, [](unsigned char ch) {
               return ch > 32 && ch < 127 && ch != '=' && ch != ':';
           });
}

// Split a capability list without copying its tokens; "a  b " yields "a", then "b".
std::vector<std::string_view> Tokens(std::string_view text) {
    std::vector<std::string_view> result;
    while (!text.empty()) {
        auto first = text.find_first_not_of(' ');
        if (first == text.npos) {
            break;
        }
        text.remove_prefix(first);
        auto size = std::min(text.size(), text.find(' '));
        result.push_back(text.substr(0, size));
        text.remove_prefix(size);
    }
    return result;
}

std::string_view Mechanism(SaslMechanism mechanism) {
    return mechanism == SaslMechanism::kPlain ? "PLAIN" : "EXTERNAL";
}

// Encode SASL bytes once before slicing the result into protocol-sized chunks.
std::string Base64(std::string_view bytes) {
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t offset = 0; offset < bytes.size(); offset += 3) {
        const auto count = std::min<std::size_t>(3, bytes.size() - offset);
        unsigned value = static_cast<unsigned char>(bytes[offset]) << 16;
        if (count > 1) {
            value |= static_cast<unsigned char>(bytes[offset + 1]) << 8;
        }
        if (count > 2) {
            value |= static_cast<unsigned char>(bytes[offset + 2]);
        }
        result += alphabet[(value >> 18) & 63];
        result += alphabet[(value >> 12) & 63];
        result += count > 1 ? alphabet[(value >> 6) & 63] : '=';
        result += count > 2 ? alphabet[value & 63] : '=';
    }
    return result;
}
} // namespace

std::expected<void, RegistrationError> ValidateRegistration(const RegistrationConfig& config) {
    if (config.caps.size() > kMaxCapabilities ||
        !std::ranges::all_of(config.caps, ValidCapability)) {
        return std::unexpected(RegistrationError::kInvalidConfig);
    }
    if (config.sasl) {
        const auto& sasl = *config.sasl;
        if ((sasl.mechanism != SaslMechanism::kPlain &&
             sasl.mechanism != SaslMechanism::kExternal) ||
            (sasl.mechanism == SaslMechanism::kPlain && sasl.username.empty()) ||
            sasl.username.find('\0') != sasl.username.npos ||
            sasl.password.find('\0') != sasl.password.npos ||
            sasl.authzid.find('\0') != sasl.authzid.npos ||
            sasl.username.size() > kMaxCredentialBytes ||
            sasl.password.size() > kMaxCredentialBytes ||
            sasl.authzid.size() > kMaxCredentialBytes) {
            return std::unexpected(RegistrationError::kInvalidConfig);
        }
        const auto bytes =
            sasl.authzid.size() + (sasl.mechanism == SaslMechanism::kPlain
                                       ? sasl.username.size() + sasl.password.size() + 2
                                       : 0);
        if (bytes > kMaxCredentialBytes) {
            return std::unexpected(RegistrationError::kInvalidConfig);
        }
    }
    return {};
}

Registration::Registration(RegistrationConfig config) : config_(std::move(config)) {
    if (auto valid = ValidateRegistration(config_); !valid) {
        error_ = valid.error();
    }
}

std::vector<std::string> Registration::Start() {
    if (std::exchange(started_, true)) {
        return {};
    }
    return {"CAP LS 302"};
}

Registration::Output Registration::Fail(RegistrationError error) {
    error_ = error;
    return std::unexpected(error);
}

bool Registration::SaslActive() const {
    return sasl_state_ == SaslState::kChallenge || sasl_state_ == SaslState::kResult;
}

void Registration::MaybeFinish(std::vector<std::string>& output) {
    if (!done_ && initial_ls_done_ && pending_.empty() && !SaslActive() &&
        (!config_.sasl || !config_.sasl->required || authenticated_)) {
        done_ = true;
        output.emplace_back("CAP END");
    }
}

Registration::Output Registration::SaslFailure(RegistrationError error, bool abort) {
    if (config_.sasl && config_.sasl->required) {
        return Fail(error);
    }
    std::vector<std::string> output;
    if (abort && SaslActive()) {
        output.emplace_back("AUTHENTICATE *");
    }
    sasl_state_ = SaslState::kFinished;
    MaybeFinish(output);
    return output;
}

bool Registration::MechanismAvailable() const {
    auto found = available_.find("sasl");
    if (found == available_.end() || !config_.sasl) {
        return false;
    }
    // An omitted mechanism list remains valid on servers implementing SASL 3.1.
    std::string_view mechanisms = found->second;
    if (mechanisms.empty()) {
        return true;
    }
    while (!mechanisms.empty()) {
        const auto size = std::min(mechanisms.size(), mechanisms.find(','));
        if (mechanisms.substr(0, size) == Mechanism(config_.sasl->mechanism)) {
            return true;
        }
        mechanisms.remove_prefix(std::min(mechanisms.size(), size + 1));
    }
    return false;
}

Registration::Output Registration::RequestDesired() {
    if (!done_ && config_.sasl && sasl_state_ == SaslState::kIdle && !MechanismAvailable()) {
        if (config_.sasl->required) {
            return Fail(RegistrationError::kSaslUnavailable);
        }
        sasl_state_ = SaslState::kFinished;
    }
    std::vector<std::string> wanted = config_.caps;
    if (!done_ && config_.sasl && sasl_state_ == SaslState::kIdle) {
        wanted.emplace_back("sasl");
    }
    std::vector<std::string> output;
    std::string request;
    for (const auto& cap : wanted) {
        if (cap == "cap-notify" || !available_.contains(cap) ||
            (cap != "sasl" && enabled_.contains(cap)) || pending_.contains(cap) ||
            rejected_.contains(cap) ||
            (cap == "sasl" && (!config_.sasl || done_ || sasl_state_ != SaslState::kIdle))) {
            continue;
        }
        if (!request.empty() && request.size() + 1 + cap.size() > kRequestBytes) {
            output.push_back("CAP REQ :" + std::exchange(request, {}));
        }
        if (!request.empty()) {
            request += ' ';
        }
        request += cap;
        pending_.insert(cap);
    }
    if (!request.empty()) {
        output.push_back("CAP REQ :" + request);
    }
    MaybeFinish(output);
    return output;
}

Registration::Output Registration::HandleCap(const MessageView& message) {
    if (message.params.size() < 3 || message.params.size() > 4 ||
        (message.params.size() == 4 && message.params[2] != "*")) {
        return Fail(RegistrationError::kProtocol);
    }
    const auto command = message.params[1];
    const auto tokens = Tokens(message.params.back());
    const bool continued = message.params.size() == 4;
    if (continued && command != "LS" && command != "LIST" && command != "ACK") {
        return Fail(RegistrationError::kProtocol);
    }
    if (command == "LS" || command == "NEW") {
        if (command == "LS" && !reading_ls_) {
            available_.clear();
            rejected_.clear();
        }
        for (auto token : tokens) {
            const auto equal = token.find('=');
            const auto name = token.substr(0, equal);
            if (!ValidCapability(name)) {
                return Fail(RegistrationError::kProtocol);
            }
            available_[std::string(name)] = equal == token.npos ? "" : token.substr(equal + 1);
            if (command == "NEW") {
                rejected_.erase(std::string(name));
            }
            if (available_.size() > kMaxCapabilities) {
                return Fail(RegistrationError::kLimit);
            }
        }
        // CAP 302 enables cap-notify implicitly, even when LS and LIST omit its name.
        enabled_.insert("cap-notify");
        if (command == "LS") {
            reading_ls_ = continued;
            if (continued) {
                return std::vector<std::string>{};
            }
            initial_ls_done_ = true;
        } else if (!initial_ls_done_) {
            return std::vector<std::string>{};
        }
        return RequestDesired();
    }
    if (command == "LIST") {
        if (!reading_list_) {
            listing_.clear();
        }
        for (auto token : tokens) {
            if (!ValidCapability(token)) {
                return Fail(RegistrationError::kProtocol);
            }
            listing_.emplace(token);
            if (listing_.size() > kMaxCapabilities) {
                return Fail(RegistrationError::kLimit);
            }
        }
        reading_list_ = continued;
        if (!continued) {
            enabled_ = std::move(listing_);
            enabled_.insert("cap-notify");
        }
        return std::vector<std::string>{};
    }
    if (command != "ACK" && command != "NAK" && command != "DEL") {
        return std::vector<std::string>{};
    }
    std::vector<std::string> changes;
    for (auto token : tokens) {
        auto name = token.starts_with('-') ? token.substr(1) : token;
        if (!ValidCapability(name)) {
            return Fail(RegistrationError::kProtocol);
        }
        changes.emplace_back(token);
    }
    if (command == "ACK") {
        acknowledging_.insert(acknowledging_.end(), changes.begin(), changes.end());
        if (acknowledging_.size() > kMaxCapabilities) {
            return Fail(RegistrationError::kLimit);
        }
        if (continued) {
            return std::vector<std::string>{};
        }
        changes = std::exchange(acknowledging_, {});
    }
    std::vector<std::string> output;
    bool sasl_rejected = false;
    bool start_sasl = false;
    for (const auto& token : changes) {
        bool removed = token.starts_with('-');
        const auto name = removed ? token.substr(1) : token;
        if (name == "cap-notify") {
            continue;
        }
        const bool requested = pending_.erase(name) != 0;
        if (command == "DEL") {
            available_.erase(name);
            enabled_.erase(name);
            rejected_.erase(name);
        } else if (command == "NAK" && requested) {
            rejected_.insert(name);
        } else if (command == "ACK") {
            if (removed) {
                enabled_.erase(name);
            } else {
                enabled_.insert(name);
            }
        }
        if (name == "sasl" && config_.sasl && !done_) {
            if (!authenticated_ &&
                (requested || start_sasl || (SaslActive() && command != "NAK")) &&
                (command != "ACK" || removed)) {
                sasl_rejected = true;
            } else if (command == "ACK" && !removed && requested &&
                       sasl_state_ == SaslState::kIdle) {
                start_sasl = true;
            }
        }
    }
    if (enabled_.size() > kMaxCapabilities) {
        return Fail(RegistrationError::kLimit);
    }
    if (sasl_rejected) {
        return SaslFailure(RegistrationError::kSaslRejected, SaslActive());
    }
    if (start_sasl && enabled_.contains("sasl")) {
        sasl_state_ = SaslState::kChallenge;
        output.emplace_back("AUTHENTICATE " + std::string(Mechanism(config_.sasl->mechanism)));
    }
    MaybeFinish(output);
    return output;
}

Registration::Output Registration::Handle(const MessageView& message) {
    if (error_) {
        return std::unexpected(*error_);
    }
    if (!started_) {
        return Fail(RegistrationError::kProtocol);
    }
    if (IsCommand(message, "908") && message.params.size() >= 2) {
        sasl_mechanisms_ = message.params[1];
    }
    if (IsCommand(message, "CAP")) {
        return HandleCap(message);
    }
    if (IsCommand(message, "001") ||
        (IsCommand(message, "421") && message.params.size() >= 2 && message.params[1] == "CAP")) {
        if (IsCommand(message, "001") && message.params.size() < 2) {
            return Fail(RegistrationError::kProtocol);
        }
        if (config_.sasl && config_.sasl->required && !authenticated_) {
            return Fail(RegistrationError::kSaslUnavailable);
        }
        if (!done_) {
            enabled_.clear();
            pending_.clear();
            rejected_.clear();
            available_.clear();
            listing_.clear();
            acknowledging_.clear();
            reading_ls_ = false;
            reading_list_ = false;
            initial_ls_done_ = false;
            done_ = true;
            sasl_state_ = SaslState::kFinished;
        }
        return std::vector<std::string>{};
    }
    if (IsCommand(message, "901")) {
        authenticated_ = false;
    }
    if (!SaslActive()) {
        return std::vector<std::string>{};
    }
    if (IsCommand(message, "AUTHENTICATE")) {
        if (sasl_state_ != SaslState::kChallenge || message.params.size() != 1 ||
            message.params.front() != "+") {
            return SaslFailure(RegistrationError::kProtocol, true);
        }
        const auto& sasl = *config_.sasl;
        std::string response = sasl.authzid;
        if (sasl.mechanism == SaslMechanism::kPlain) {
            response += '\0';
            response += sasl.username;
            response += '\0';
            response += sasl.password;
        }
        const auto encoded = Base64(response);
        std::vector<std::string> output;
        for (std::size_t offset = 0; offset < encoded.size(); offset += 400) {
            output.push_back("AUTHENTICATE " + encoded.substr(offset, 400));
        }
        if (encoded.size() % 400 == 0) {
            output.emplace_back("AUTHENTICATE +");
        }
        sasl_state_ = SaslState::kResult;
        return output;
    }
    if (IsCommand(message, "903")) {
        if (message.params.size() < 2) {
            return SaslFailure(RegistrationError::kProtocol, true);
        }
        authenticated_ = true;
        sasl_state_ = SaslState::kFinished;
        std::vector<std::string> output;
        MaybeFinish(output);
        return output;
    }
    for (auto numeric : {"902", "904", "905", "906", "907"}) {
        if (IsCommand(message, numeric)) {
            return SaslFailure(RegistrationError::kSaslFailed);
        }
    }
    return std::vector<std::string>{};
}
} // namespace ircbot
