#pragma once

#include "utils/log.hpp"
#include "utils/iconv.hpp"
#include "utils/entities.hpp"
#include <boost/algorithm/string.hpp>
#include <experimental/optional>
#include <functional>
#include <string>
#include <memory>

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
    TitleParser(std::function<void(std::string)> fn);

    void setCallback(std::function<void(std::string)> fn);

    void setConverter(std::unique_ptr<iconvpp::converter> conv_ptr);

    template <typename Iter>
    bool parse(Iter beg, Iter end) {
        while (beg != end) {
            if (consume(*beg++)) {
                if (callback_) {
                    // charset
                    if (conv_ptr_) {
                        std::string str;
                        conv_ptr_->convert(content_, str);
                        content_.swap(str);
                    }

                    // html unescape
                    content_ = Html::Unescape(std::move(content_));

                    // remove blank characters
                    content_.erase(
                        std::remove_if(
                            content_.begin(), content_.end(),
                            [](char c) { return c == '\r' || c == '\n'; }),
                        content_.end());

                    // send to irc channel
                    callback_(boost::trim_right_copy(
                        boost::trim_left_copy(std::move(content_))));
                }
                DEBUG("Found!");
                return true;
            }
        }
        return false;
    }

private:
    bool consume(char input);

private:
    std::string content_;
    std::function<void(std::string)> callback_;

    std::unique_ptr<iconvpp::converter> conv_ptr_;
};
