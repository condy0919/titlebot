#include "title_parser.hpp"
#include <boost/beast/core/string.hpp>
#include <cctype>
#include <utility>

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
        } else if (!std::isspace(static_cast<unsigned char>(ch))) {
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
        if (quote_) {
            if (ch == quote_) {
                quote_ = 0;
            }
        } else if (ch == '>') {
            state_ = State::RightAngleBracket;
        } else if (std::isspace(static_cast<unsigned char>(ch))) {
            attributes_ = true;
        } else if (!attributes_) {
            state_ = State::Other;
        } else if (ch == '\'' || ch == '"') {
            quote_ = ch;
        }
        break;

    case State::RightAngleBracket:
        if (ch == '>') {
            auto end = title_.find_last_not_of(" \t\r\n\f");
            constexpr std::string_view close = "</title";
            if (end != title_.npos && end + 1 >= close.size()) {
                auto start = end + 1 - close.size();
                if (boost::beast::iequals(std::string_view(title_).substr(start, close.size()),
                                          close)) {
                    title_.resize(start);
                    state_ = State::Other;
                    attributes_ = false;
                    return std::exchange(title_, {});
                }
            }
        }
        title_.push_back(ch);
        break;
    }

    return std::nullopt;
}

} // namespace tb
