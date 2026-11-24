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
    bool attributes_ = false;
    char quote_ = 0;
    std::string title_;
};
} // namespace tb

#endif
