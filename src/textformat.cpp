#include "textformat.hpp"
#include <cctype>
#include <algorithm>

namespace IRC {

std::string formatNormalize(std::string s) {
    std::string ret;

    enum {
        OTHER,
        PREFIX_03,
        COLOR_FG_1,
        COLOR_FG_2,
        COMMA,
        COLOR_BG_1
    } state = OTHER;

    // remove all colors
#define AMONG_0_5(c) (c == '0' || c == '1' || c == '2' || c == '3' || c == '4' || c == '5')
#define AMONG_0_9(c) (AMONG_0_5(c) || c == '6' || c == '7' || c == '8' || c == '9')
    for (auto c : s) {
        if (state == OTHER) {
            if (c == '\x03') {
                state = PREFIX_03;
            } else {
                ret += c;
            }
            continue;
        }

        switch (state) {
        case PREFIX_03:
            if (!AMONG_0_9(c)) {
                ret += c; // eat!
                state = OTHER;
            } else {
                state = COLOR_FG_1;
            }
            break;

        case COLOR_FG_1:
            if (AMONG_0_9(c)) {
                state = COLOR_FG_2;
            } else if (c == ',') {
                state = COMMA;
            } else {
                ret += c; // eat!
                state = OTHER;
            }
            break;

        case COLOR_FG_2:
            if (c == ',') {
                state = COMMA;
            } else {
                ret += c; // eat!
                state = OTHER;
            }
            break;
        
        case COMMA:
            if (AMONG_0_9(c)) {
                state = COLOR_BG_1;
            } else {
                ret += ','; // eat!
                ret += c;
                state = OTHER;
            }
            break;

        case COLOR_BG_1:
            if (AMONG_0_9(c)) {
                state = OTHER;
            } else {
                ret += c; // eat!
            }
            break;

        default:
            break;
        }
    }
    if (state == COMMA) {
        ret += ',';
    }
#undef AMONG_0_5
#undef AMONG_0_9

    // remove format prefix
    ret.erase(std::remove_if(ret.begin(), ret.end(),
                             [](char c) { return !std::isprint(c); }),
              ret.end());
    return ret;
}
}
