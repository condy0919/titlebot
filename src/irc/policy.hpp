#ifndef TITLEBOT_IRC_POLICY_HPP_
#define TITLEBOT_IRC_POLICY_HPP_
#include <chrono>
#include <cstddef>
#include <list>
#include <optional>
#include <string>
#include <string_view>

#include "common/runtime_limits.hpp"
#include "ircbot/features.hpp"
namespace tb {
/// Track a fixed window for each IRC target and discard expired entries.
class IrcReplyPolicy {
public:
    IrcReplyPolicy(ReplyLimits limits = {}) : limits_(limits) {}

    /// Consume one of two replies allowed per target in a two-second window.
    /// For example, three calls to Allow("#room", now) on a fresh limiter yield true, true, false.
    bool Allow(std::string target, std::chrono::steady_clock::time_point now,
               ircbot::CaseMapping mapping = ircbot::CaseMapping::kRfc1459);

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

} // namespace tb
#endif // TITLEBOT_IRC_POLICY_HPP_
