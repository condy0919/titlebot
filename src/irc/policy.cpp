#include "policy.hpp"

#include <utility>

namespace tb {
bool IrcReplyPolicy::Allow(std::string target, std::chrono::steady_clock::time_point now,
                           ircbot::CaseMapping mapping) {
    if (now >= burst_until_) {
        burst_until_ = now + limits_.window;
        burst_ = 0;
    }
    if (burst_ >= limits_.per_session) {
        return false;
    }
    while (!windows_.empty() && windows_.front().expires <= now) {
        windows_.pop_front();
    }
    for (auto& window : windows_) {
        if (ircbot::Equals(window.target, target, mapping)) {
            if (window.sent >= limits_.per_target) {
                return false;
            }
            ++window.sent;
            ++burst_;
            return true;
        }
    }
    if (!limits_.per_target || windows_.size() >= limits_.targets) {
        return false;
    }
    windows_.push_back({
        .target = std::move(target),
        .expires = now + limits_.window,
        .sent = 1,
    });
    ++burst_;
    return true;
}
std::optional<std::string> FormatReplyLine(const std::string& target, std::string_view title,
                                           std::size_t line_bytes) {
    auto prefix = "PRIVMSG " + target + " :";
    if (title.empty()) {
        return {};
    }
    if (prefix.size() >= line_bytes) {
        return {};
    }

    // If the first excluded byte is a UTF-8 continuation byte (10xxxxxx), the cut
    // splits a code point. Move the cut back to its leading byte so it is omitted in full.
    auto limit = line_bytes - prefix.size();
    if (title.size() > limit) {
        while (limit && (static_cast<unsigned char>(title[limit]) & 192) == 128) {
            --limit;
        }
        title = title.substr(0, limit);
    }

    if (title.empty()) {
        return {};
    }

    auto line = std::move(prefix);
    // Replace ASCII control bytes (0x00-0x1F and DEL) with spaces so a title
    // cannot insert a line break or change IRC text formatting.
    for (char ch : title) {
        line += static_cast<unsigned char>(ch) < 32 || ch == 127 ? ' ' : ch;
    }
    return line;
}
} // namespace tb
