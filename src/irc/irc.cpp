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
    const auto in = [](char ch, char from, char to) { return ch >= from && ch <= to; };

    enum {
        NORMAL,
        PREFIX_0x3,
        COLOR_FG_1,
        COLOR_FG_2,
        COMMA,
        COLOR_BG_1,
    } state = NORMAL;
    std::string out;
    for (char c : s) {
        switch (state) {
        case NORMAL:
            if (c == '\x03') {
                state = PREFIX_0x3;
            } else {
                out += c;
            }
            break;

        case PREFIX_0x3:
            if (!in(c, '0', '9')) {
                out += c;
                state = NORMAL;
            } else {
                state = COLOR_FG_1;
            }
            break;

        case COLOR_FG_1:
            if (in(c, '0', '9')) {
                state = COLOR_FG_2;
            } else if (c == ',') {
                state = COMMA;
            } else {
                out += c;
                state = NORMAL;
            }
            break;

        case COLOR_FG_2:
            if (c == ',') {
                state = COMMA;
            } else {
                out += c;
                state = NORMAL;
            }
            break;

        case COMMA:
            if (in(c, '0', '9')) {
                state = COLOR_BG_1;
            } else {
                out += ',';
                out += c;
                state = NORMAL;
            }
            break;

        case COLOR_BG_1:
            if (in(c, '0', '9')) {
                state = NORMAL;
            } else {
                out += c;
            }
            break;
        }
    }

    if (state == COMMA) {
        out += ',';
    }

    // Remove format prefix.
    std::erase_if(out, [](char c) { return (static_cast<unsigned char>(c) < 32 && c != '\t') || c == 127; });
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

    // [s.begin()+1, from_end) => who
    const auto from_end = std::find(s.begin() + 1, s.end(), '!');
    if (from_end == s.end()) {
        return std::nullopt;
    }

    const auto privmsg_start = std::find(from_end, s.end(), ' ');
    if (privmsg_start == s.end()) {
        return std::nullopt;
    }

    if (s.compare(privmsg_start - s.begin(), 9, " PRIVMSG ") != 0) {
        return std::nullopt;
    }

    // [privmsg_start+9, target_end) => target
    const auto target_end = std::find(privmsg_start + 9, s.end(), ' ');
    if (target_end == s.end()) {
        return std::nullopt;
    }

    // the private message is empty
    if (std::distance(target_end, s.end()) <= 2) {
        return std::nullopt;
    }

    // remove color/bold/italic/underline... text properties
    const std::string norm = normalize(std::string(target_end + 2, s.end()));

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
                    .who = std::string(s.begin() + 1, from_end),
                    .channel = std::string(privmsg_start + 9, target_end),
                    .protocol = https ? "https" : "http",
                    .url = std::move(url),
                };
            }
            break;
        }
    }

    if (!url.empty()) {
        return PrivMsg{
            .who = std::string(s.begin() + 1, from_end),
            .channel = std::string(privmsg_start + 9, target_end),
            .protocol = https ? "https" : "http",
            .url = std::move(url),
        };
    }
    return std::nullopt;
}
} // namespace tb
