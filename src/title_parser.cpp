#include "title_parser.hpp"

namespace tb {

std::optional<std::string> TitleParser::consume(char ch) {
    switch (state_) {
    case State::Other:
        if (ch == '<') {
            state_ = State::LeftAngleBracket;
        }
        break;

    case State::LeftAngleBracket:
        if (ch == 't' || ch == 'T') {
            state_ = State::CharT1;
        } else if (!std::isspace(ch)) {
            state_ = State::Other;
        }
        break;

    case State::CharT1:
        state_ = (ch == 'i' || ch == 'I') ? State::CharI : State::Other;
        break;

    case State::CharI:
        state_ = (ch == 't' || ch == 'T') ? State::CharT2 : State::Other;
        break;

    case State::CharT2:
        state_ = (ch == 'l' || ch == 'L') ? State::CharL : State::Other;
        break;

    case State::CharL:
        state_ = (ch == 'e' || ch == 'E') ? State::CharE : State::Other;
        break;

    case State::CharE:
        state_ = (ch == '>') ? State::RightAngleBracket : State::CharE;
        break;

    case State::RightAngleBracket:
        if (ch != '<') {
            title_.push_back(ch);
        } else {
            return std::move(title_);
        }
        break;
    }

    return std::nullopt;
}

} // namespace tb

// TitleParser::TitleParser() : state_(TOKEN_OTHER) {}

// TitleParser::TitleParser(std::function<void(std::string)> fn)
//     : state_(TOKEN_OTHER), callback_(std::move(fn)) {}

// void TitleParser::setCallback(std::function<void(std::string)> fn) {
//     callback_ = std::move(fn);
// }

// void TitleParser::setCharsetDecoder(std::unique_ptr<iconvpp::converter> p) {
//     conv_ptr_ = std::move(p);
// }
