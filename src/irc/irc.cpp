#include "irc.hpp"

#include <algorithm>
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
    auto token = [&]() {
        auto end = rest.find(' ');
        auto word = rest.substr(0, end);
        rest.remove_prefix(word.size());
        while (rest.starts_with(' ')) {
            rest.remove_prefix(1);
        }
        return word;
    };
    auto prefix = token().substr(1);
    auto bang = prefix.find('!');
    if (bang == prefix.npos || !bang || token() != "PRIVMSG") {
        return std::nullopt;
    }
    auto who = prefix.substr(0, bang);
    auto target = token();
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

    std::string url;
    bool https = false;
    enum {
        Other,
        CharH,
        CharT1,
        CharT2,
        CharP,
        CharS,
        Colon,
        Slash1,
        Slash2,
        Url,
    } state = Other;
    for (char c : norm) {
        switch (state) {
        case Other:
            https = false;
            if (c == 'h' || c == 'H') {
                state = CharH;
            }
            break;

        case CharH:
            state = (c == 't' || c == 'T') ? CharT1 : Other;
            break;

        case CharT1:
            state = (c == 't' || c == 'T') ? CharT2 : Other;
            break;

        case CharT2:
            state = (c == 'p' || c == 'P') ? CharP : Other;
            break;

        case CharP:
            if (c == 's' || c == 'S') {
                https = true;
                state = CharS;
            } else if (c == ':') {
                state = Colon;
            } else {
                state = Other;
            }
            break;

        case CharS:
            state = (c == ':') ? Colon : Other;
            break;

        case Colon:
            state = (c == '/') ? Slash1 : Other;
            break;

        case Slash1:
            state = (c == '/') ? Slash2 : Other;
            break;

        case Slash2:
            if (std::isalnum(static_cast<unsigned char>(c))) {
                url.push_back(c);
                state = Url;
            } else {
                state = Other;
            }
            break;

        case Url:
            if (static_cast<unsigned char>(c) < 128 && !is_tspecial(c)) {
                url.push_back(c);
            } else {
                return PrivMsg{
                    .who = std::string(who),
                    .channel = std::string(target),
                    .protocol = https ? "https" : "http",
                    .url = std::move(url),
                };
            }
            break;
        }
    }

    if (!url.empty()) {
        return PrivMsg{
            .who = std::string(who),
            .channel = std::string(target),
            .protocol = https ? "https" : "http",
            .url = std::move(url),
        };
    }
    return std::nullopt;
}
} // namespace tb
