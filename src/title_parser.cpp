#include "title_parser.hpp"

TitleParser::TitleParser() : state_(TOKEN_OTHER) {}

TitleParser::TitleParser(std::function<void(std::string)> fn)
    : state_(TOKEN_OTHER), callback_(std::move(fn)) {}

void TitleParser::setCallback(std::function<void(std::string)> fn) {
    callback_ = std::move(fn);
}

void TitleParser::setCharsetDecoder(std::unique_ptr<iconvpp::converter> p) {
    conv_ptr_ = std::move(p);
}

bool TitleParser::consume(char input) {
    switch (state_) {
    case TOKEN_OTHER:
        if (input == '<') {
            state_ = TOKEN_LP_SP;
        }
        break;

    case TOKEN_LP_SP:
        if (input == 't' || input == 'T') {
            state_ = TOKEN_T_1;
        } else if (!std::isspace(input)) {
            state_ = TOKEN_OTHER;
        }
        break;

    case TOKEN_LP:
        if (input == 't' || input == 'T') {
            state_ = TOKEN_T_1;
        } else {
            state_ = TOKEN_OTHER;
        }
        break;

    case TOKEN_T_1:
        if (input == 'i' || input == 'I') {
            state_ = TOKEN_I;
        } else {
            state_ = TOKEN_OTHER;
        }
        break;

    case TOKEN_I:
        if (input == 't' || input == 'T') {
            state_ = TOKEN_T_2;
        } else {
            state_ = TOKEN_OTHER;
        }
        break;

    case TOKEN_T_2:
        if (input == 'l' || input == 'L') {
            state_ = TOKEN_L;
        } else {
            state_ = TOKEN_OTHER;
        }
        break;

    case TOKEN_L:
        if (input == 'e' || input == 'E') {
            state_ = TOKEN_E;
        } else {
            state_ = TOKEN_OTHER;
        }
        break;

    case TOKEN_E:
        if (input == '>') {
            state_ = TOKEN_RP;
        } else {
            state_ = TOKEN_PROPERTIES;
        }
        break;

    case TOKEN_PROPERTIES:
        if (input == '>') {
            state_ = TOKEN_RP;
        }
        break;

    case TOKEN_RP:
        if (input != '<') {
            content_.push_back(input);
            state_ = TOKEN_CONTENT_NO_LP;
        } else {
            return true;
        }
        break;

    case TOKEN_CONTENT_NO_LP:
        if (input != '<') {
            content_.push_back(input);
        } else {
            return true;
        }
        break;

    default:
        return false;
    }
    return false;
}
