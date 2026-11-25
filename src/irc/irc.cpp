#include "irc.hpp"

#include <algorithm>
#include <cctype>

#include <boost/beast/core/string.hpp>

#include "ircbot/features.hpp"

namespace {
// HTTP chars that need quoting.
constexpr bool is_tspecial(char c) {
    switch (c) {
    case '(':
    case ')':
    case '<':
    case '>':
    case '\\':
    case '\"':
    case '[':
    case ']':
    case '{':
    case '}':
    case ' ':
    case '\t':
        return true;

    default:
        return false;
    }
}
} // namespace

namespace tb {
char FoldIrcCase(char c) {
    return ircbot::FoldCase(c, ircbot::CaseMapping::kRfc1459);
}

std::string_view TakeIrcToken(std::string_view& rest) {
    auto end = rest.find(' ');
    auto word = rest.substr(0, end);
    rest.remove_prefix(word.size());
    while (rest.starts_with(' ')) {
        rest.remove_prefix(1);
    }
    return word;
}

// Strip IRC formatting before looking for links. For the color parameter syntax, see
// https://modern.ircdocs.horse/formatting#color and
// https://modern.ircdocs.horse/formatting#hex-color
std::string normalize(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        auto c = static_cast<unsigned char>(s[i++]);
        // IRC color control bytes: 0x03 takes a palette index (1 or 2 decimal digits),
        // and 0x04 takes an RGB value (6 hex digits). Strip the control byte and any
        // foreground color, including a comma and background color if both follow.
        if (c == 3 || c == 4) {
            const size_t width = c == 3 ? 2 : 6;
            auto digits = [&](size_t pos) {
                size_t n = 0;
                while (pos + n < s.size() && n < width) {
                    auto ch = static_cast<unsigned char>(s[pos + n]);
                    if (!(c == 3 ? std::isdigit(ch) : std::isxdigit(ch))) {
                        break;
                    }
                    ++n;
                }
                return c == 4 && n != width ? size_t{0} : n;
            };
            auto foreground = digits(i);
            i += foreground;
            if (foreground && i < s.size() && s[i] == ',') {
                if (auto background = digits(i + 1)) {
                    i += 1 + background;
                }
            }
        } else if ((c >= 32 && c != 127) || c == '\t') {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

// ":condy!~condy@unaffiliated/condy PRIVMSG #linuxba :http://www.baidu.com"
//
// from: condy
// target: #linuxba
// schema: http
// url: www.baidu.com
std::optional<PrivMsg> parse_privmsg(std::string_view s) {
    auto message = ircbot::ParseMessage(s);
    if (!message) {
        return std::nullopt;
    }
    return parse_privmsg(*message);
}

std::optional<PrivMsg> parse_privmsg(const ircbot::MessageView& message) {
    auto prefix = message.prefix;
    auto bang = prefix.find('!');
    if (bang == prefix.npos || !bang || !ircbot::IsCommand(message, "PRIVMSG") ||
        message.params.size() != 2) {
        return std::nullopt;
    }
    auto who = message.Nick();
    auto target = message.params[0];
    auto rest = message.params[1];
    auto control = [](unsigned char c) { return c < 32 || c == 127; };
    if (target.empty() || target.starts_with(':') || std::ranges::any_of(prefix, control) ||
        std::ranges::any_of(target, control) || rest.empty()) {
        return std::nullopt;
    }
    std::string normalized;
    if (std::ranges::any_of(rest, control)) {
        if (rest.find_first_of("hH") == rest.npos) {
            return std::nullopt;
        }
        normalized = normalize(rest);
        rest = normalized;
    }
    auto norm = rest;

    for (auto pos = norm.find_first_of("hH"); pos != norm.npos;
         pos = norm.find_first_of("hH", pos + 1)) {
        std::string_view tail(norm.data() + pos, norm.size() - pos);
        bool secure = boost::beast::iequals(tail.substr(0, 8), "https://");
        if (!secure && !boost::beast::iequals(tail.substr(0, 7), "http://")) {
            continue;
        }
        tail.remove_prefix(secure ? 8 : 7);
        if (tail.empty() || !std::isalnum(static_cast<unsigned char>(tail.front()))) {
            continue;
        }
        auto end = tail.begin();
        auto opening = end;
        unsigned depth = 0;
        for (; end != tail.end(); ++end) {
            if (*end == '(') {
                if (depth++ == 0) {
                    opening = end;
                }
            } else if (*end == ')' && depth) {
                --depth;
            } else if (static_cast<unsigned char>(*end) >= 128 || is_tspecial(*end)) {
                break;
            }
        }
        if (depth) {
            end = opening;
        }
        constexpr std::string_view trailing = ".,;:!?'";
        while (end != tail.begin() && trailing.find(end[-1]) != trailing.npos) {
            --end;
        }
        return PrivMsg{
            .who = std::string(who),
            .channel = std::string(target),
            .protocol = secure ? "https" : "http",
            .url = std::string(tail.begin(), end),
        };
    }
    return std::nullopt;
}
} // namespace tb
