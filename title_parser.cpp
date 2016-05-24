#include "title_parser.hpp"

TitleParser::TitleParser() : state_(TOKEN_OTHER) {}

bool TitleParser::consume(char input) {
    switch (state_) {
    case TOKEN_OTHER:
        if (input == '<') {
            state_ = TOKEN_LP;
        }
        break;

    case TOKEN_LP:
        if (input == 't') {
            state_ = TOKEN_T_1;
        } else {
            state_ = TOKEN_OTHER;
        }
        break;

    case TOKEN_T_1:
        if (input == 'i') {
            state_ = TOKEN_I;
        } else {
            state_ = TOKEN_OTHER;
        }
        break;

    case TOKEN_I:
        if (input == 't') {
            state_ = TOKEN_T_2;
        } else {
            state_ = TOKEN_OTHER;
        }
        break;

    case TOKEN_T_2:
        if (input == 'l') {
            state_ = TOKEN_L;
        } else {
            state_ = TOKEN_OTHER;
        }
        break;

    case TOKEN_L:
        if (input == 'e') {
            state_ = TOKEN_E;
        } else {
            state_ = TOKEN_OTHER;
        }
        break;

    case TOKEN_E:
        if (input == '>') {
            state_ = TOKEN_RP;
        } else {
            state_ = TOKEN_OTHER;
        }
        break;

    case TOKEN_RP:
        if (input != '<') {
            if (!isspace(input))
                content_.push_back(input);
            state_ = TOKEN_CONTENT_NO_LP;
        } else {
            return true;
        }
        break;

    case TOKEN_CONTENT_NO_LP:
        if (input != '<') {
            if (!isspace(input))
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
