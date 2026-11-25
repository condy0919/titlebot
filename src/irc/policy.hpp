#ifndef TITLEBOT_IRC_POLICY_HPP_
#define TITLEBOT_IRC_POLICY_HPP_
#include <chrono>
#include <list>
#include <optional>
#include <string>
#include <string_view>

#include "common/runtime_limits.hpp"
namespace tb {
/// Track a fixed window for each IRC target and discard expired entries.
class IrcReplyPolicy {
public:
    IrcReplyPolicy(ReplyLimits limits = {}) : limits_(limits) {}

    /// Consume one of two replies allowed per target in a two-second window.
    /// For example, three calls to Allow("#room", now) on a fresh limiter yield true, true, false.
    bool Allow(std::string target, std::chrono::steady_clock::time_point now);

private:
    struct Window {
        std::string target;
        std::chrono::steady_clock::time_point expires;
        unsigned sent;
    };

    std::chrono::steady_clock::time_point burst_until_{};
    unsigned burst_ = 0;
    ReplyLimits limits_;
    std::list<Window> windows_;
};

/// Format a complete IRC reply without CRLF, e.g. ("#r", "a\nb", 64) yields "PRIVMSG #r :a b".
std::optional<std::string> FormatReplyLine(const std::string& target, std::string_view title,
                                           std::size_t line_bytes);

class NicknameNegotiator {
public:
    /// Start a registration, retaining the last advertised limit across reconnects.
    void Begin() {
        suffix_.reset();
        retries_ = 0;
        last_.clear();
    }
    /// For example, Observe("bot NICKLEN=12 :supported") limits subsequent retries to 12 bytes.
    void Observe(std::string_view params);
    /// Return the next candidate or nullopt when a distinct suffix no longer fits.
    std::optional<std::string> Retry(const std::string& nick, std::string_view rejected);

private:
    std::size_t limit_ = 9;
    std::optional<unsigned> suffix_;
    unsigned retries_ = 0;
    std::string last_;
};
} // namespace tb
#endif // TITLEBOT_IRC_POLICY_HPP_
