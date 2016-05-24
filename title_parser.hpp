#pragma once

#include <experimental/optional>
#include <string>

class TitleParser {
    enum state {
        TOKEN_OTHER,
        TOKEN_LP,             // <
        TOKEN_T_1,            // t
        TOKEN_I,              // i
        TOKEN_T_2,            // t
        TOKEN_L,              // l
        TOKEN_E,              // e
        TOKEN_RP,             // >
        TOKEN_CONTENT_NO_LP,  // xxx
    } state_;

public:
    TitleParser();

    template <typename Iter>
    std::experimental::optional<std::string> parse(Iter beg, Iter end) {
        while (beg != end) {
            if (consume(*beg++)) {
                // ok
                return content_;
            }
        }
        return {};
    }

private:
    bool consume(char input);

private:
    std::string content_;
};
