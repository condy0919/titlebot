#include "irc.hpp"
#include <cctype>

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
    std::erase_if(out, [](char c) { return !std::isprint(c); });
    return out;
}
} // namespace tb
