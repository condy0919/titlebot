#ifndef TITLEBOT_TITLE_PARSER_HPP_
#define TITLEBOT_TITLE_PARSER_HPP_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace tb {
class TitleParser {
public:
    template <typename Iter>
    std::optional<std::string> parse(Iter begin, Iter end) {
        while (begin != end) {
            if (auto result = ReadByte(*begin++)) {
                return result;
            }
        }
        return std::nullopt;
    }

    std::string_view sample() const {
        return head_;
    }

    std::string DeclaredCharset() const {
        return charset_;
    }

    bool unicode_bom() const {
        return utf16_ || head_.starts_with("\xef\xbb\xbf");
    }

private:
    enum class State : std::uint8_t {
        kText,
        kTag,
        kComment,
        kTitle,
    };

    std::optional<std::string> ReadByte(char ch);
    std::optional<std::string> consume(char ch);

    std::string head_;
    int utf16_ = 0;
    int first_byte_ = -1;
    unsigned surrogate_ = 0;

    State state_ = State::kText;
    std::string tag_;
    std::string charset_;
    std::string raw_element_;
    unsigned template_depth_ = 0;
    unsigned svg_depth_ = 0;
    unsigned dashes_ = 0;
    char quote_ = 0;
    std::string title_;
};
} // namespace tb

#endif // TITLEBOT_TITLE_PARSER_HPP_
