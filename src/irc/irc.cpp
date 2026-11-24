#include "irc.hpp"

#include <algorithm>
#include <boost/beast/core/string.hpp>
#include <boost/url/ipv6_address.hpp>
#include <cctype>

namespace {
// HTTP chars that need quoting.
inline constexpr bool is_tspecial(char c) {
    switch (c) {
    case '(':
    case ')':
    case '<':
    case '>':
    case '@':
    case ',':
    case ';':
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
std::string_view TakeIrcToken(std::string_view& rest) {
    auto end = rest.find(' ');
    auto word = rest.substr(0, end);
    rest.remove_prefix(word.size());
    while (rest.starts_with(' ')) {
        rest.remove_prefix(1);
    }
    return word;
}

std::string normalize(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        auto c = static_cast<unsigned char>(s[i++]);
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
std::optional<PrivMsg> parse_privmsg(const std::string& s) {
    if (s.empty() || s.front() != ':') {
        return std::nullopt;
    }

    std::string_view rest(s);
    auto prefix = TakeIrcToken(rest).substr(1);
    auto bang = prefix.find('!');
    if (bang == prefix.npos || !bang || TakeIrcToken(rest) != "PRIVMSG") {
        return std::nullopt;
    }
    auto who = prefix.substr(0, bang);
    auto target = TakeIrcToken(rest);
    auto control = [](unsigned char c) { return c < 32 || c == 127; };
    if (target.empty() || target.starts_with(':') || std::ranges::any_of(prefix, control) ||
        std::ranges::any_of(target, control) || rest.empty()) {
        return std::nullopt;
    }
    if (rest.starts_with(':')) {
        rest.remove_prefix(1);
    } else if (rest.find(' ') != rest.npos) {
        return std::nullopt;
    }
    const auto norm = normalize(std::string(rest));

    for (auto pos = norm.find_first_of("hH"); pos != norm.npos;
         pos = norm.find_first_of("hH", pos + 1)) {
        std::string_view tail(norm.data() + pos, norm.size() - pos);
        bool secure = boost::beast::iequals(tail.substr(0, 8), "https://");
        if (!secure && !boost::beast::iequals(tail.substr(0, 7), "http://")) {
            continue;
        }
        tail.remove_prefix(secure ? 8 : 7);
        if (tail.empty() ||
            (tail.front() != '[' && !std::isalnum(static_cast<unsigned char>(tail.front())))) {
            continue;
        }
        auto suffix = tail;
        if (tail.starts_with('[')) {
            auto close = tail.find(']');
            if (close == tail.npos || !boost::urls::parse_ipv6_address(tail.substr(1, close - 1))) {
                continue;
            }
            suffix.remove_prefix(close + 1);
        }
        auto end = suffix.begin();
        auto opening = end;
        unsigned depth = 0;
        for (; end != suffix.end(); ++end) {
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
        return PrivMsg{std::string(who), std::string(target), secure ? "https" : "http",
                       std::string(tail.begin(), end)};
    }
    return std::nullopt;
}
} // namespace tb
