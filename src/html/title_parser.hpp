#ifndef TITLEBOT_TITLE_PARSER_HPP_
#define TITLEBOT_TITLE_PARSER_HPP_

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace tb {
class TitleParser {
public:
    template <typename Iter>
    std::optional<std::string> parse(Iter begin, Iter end) {
        while (begin != end) {
            if (auto result = consume(*begin++)) {
                return result;
            }
        }
        return std::nullopt;
    }

private:
    std::optional<std::string> consume(char ch);

    enum class State {
        Other,
        LeftAngleBracket,  // < WS*
        CharT1,            // t
        CharI,             // i
        CharT2,            // t
        CharL,             // l
        CharE,             // e WS*
        RightAngleBracket, // >
    } state_ = State::Other;
    std::string title_;
};
} // namespace tb

#endif // TITLEBOT_TITLE_PARSER_HPP_

// #include "utils/entities.hpp"
// #include "utils/utf8tran.hpp"
// #include <boost/algorithm/string.hpp>
// #include <experimental/optional>

// public:
//     TitleParser();
//     TitleParser(std::function<void(std::string)> fn);

//     void setCallback(std::function<void(std::string)> fn);

//     void setCharsetDecoder(std::unique_ptr<iconvpp::converter> p);

//     template <typename Iter>
//     bool parse(Iter beg, Iter end) {
//         while (beg != end) {
//             if (consume(*beg++)) {
//                 if (callback_) {
//                     // UTF-8 always
//                     if (conv_ptr_) {
//                         // charset found in `Content-Type' field.
//                         std::string tmp;
//                         conv_ptr_->convert(content_, tmp);
//                         tmp.swap(content_);
//                     } else {
//                         // guess
//                         content_ = translator_.trans(std::move(content_));
//                     }

//                     // html unescape
//                     content_ = Html::Unescape(std::move(content_));

//                     // remove blank characters
//                     content_.erase(std::remove_if(content_.begin(), content_.end(),
//                                                   [](char c) { return c == '\r' || c == '\n'; }),
//                                    content_.end());

//                     // send to irc channel
//                     callback_(boost::trim_right_copy(boost::trim_left_copy(std::move(content_))));
//                 }
//                 return true;
//             }
//         }
//         return false;
//     }

// private:
//     bool consume(char input);

// private:
//     std::string content_;
//     std::function<void(std::string)> callback_;

//     std::unique_ptr<iconvpp::converter> conv_ptr_;
//     UTF8Translator translator_;
// };
