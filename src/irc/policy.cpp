#include "policy.hpp"

#include <algorithm>
#include <charconv>
#include <mutex>
#include <random>

#include "irc.hpp"
namespace tb {
bool IrcReplyPolicy::Allow(std::string target, std::chrono::steady_clock::time_point now) {
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
    for (char& ch : target) {
        ch = FoldIrcCase(ch);
    }
    for (auto& window : windows_) {
        if (window.target == target) {
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
namespace {
unsigned RandomNickSuffix() {
    static std::mt19937 generator{std::random_device{}()};
    static std::mutex mutex;
    std::scoped_lock lock(mutex);
    return std::uniform_int_distribution<unsigned>(0, 65535)(generator);
}

} // namespace

void NicknameNegotiator::Observe(std::string_view params) {
    TakeIrcToken(params); // Skip the recipient; tokens end at the trailing description.
    while (!params.empty() && !params.starts_with(':')) {
        auto token = TakeIrcToken(params);
        if (token == "-NICKLEN") {
            limit_ = 9;
        } else if (token.starts_with("NICKLEN=")) {
            token.remove_prefix(std::string_view("NICKLEN=").size());
            std::size_t value = 0;
            auto end = token.data() + token.size();
            auto [ptr, ec] = std::from_chars(token.data(), end, value);
            if (ec == std::errc{} && ptr == end && value > 0) {
                limit_ = value;
            }
        }
    }
}

std::optional<std::string> NicknameNegotiator::Retry(const std::string& nick,
                                                     std::string_view rejected) {
    // Before ISUPPORT, a 433 may echo the server-truncated form of our last NICK.
    const auto& requested = last_.empty() ? nick : last_;
    if (!rejected.empty() && rejected.size() < requested.size() &&
        requested.starts_with(rejected)) {
        limit_ = std::min(limit_, rejected.size());
    }
    // Keep one leading character. Stop before a short decimal suffix cycles.
    if (std::to_string(retries_).size() >= limit_) {
        return {};
    }
    if (!suffix_) {
        suffix_ = RandomNickSuffix();
    }
    auto suffix = std::to_string(*suffix_ + retries_++);
    if (suffix.size() >= limit_) {
        suffix.erase(0, suffix.size() - (limit_ - 1));
    }
    auto keep = std::min(nick.size(), limit_ - suffix.size());
    while (keep && keep < nick.size() && (static_cast<unsigned char>(nick[keep]) & 0xc0) == 0x80) {
        --keep;
    }
    auto prefix = keep ? nick.substr(0, keep) : "_";
    last_ = prefix + suffix;
    return last_;
}

} // namespace tb
