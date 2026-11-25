#ifndef TITLEBOT_TITLE_PARSER_HPP_
#define TITLEBOT_TITLE_PARSER_HPP_

#include <concepts>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "document_bytes.hpp"

namespace tb {
class TitleParser {
public:
    /// Feed successive ranges from one document. Return the raw title when its closing tag
    /// arrives. For example, calling parse on "<title>A &amp;" yields std::nullopt;
    /// calling parse on " B</title>" with the same parser then yields "A &amp; B".
    template <typename Iter>
    std::optional<std::string> parse(Iter begin, Iter end) {
        while (begin != end) {
            if constexpr (std::contiguous_iterator<Iter> &&
                          std::same_as<std::iter_value_t<Iter>, char>) {
                if (state_ == State::kText && bytes_.can_skip_bytes()) {
                    std::string_view rest(std::to_address(begin), end - begin);
                    auto next = rest.find('<');
                    if (next == rest.npos) {
                        break;
                    }
                    begin += next;
                }
            }
            if (auto result = ReadByte(*begin++)) {
                return result;
            }
        }
        return std::nullopt;
    }

    /// 在文档结束时尝试从未闭合的属性引号后恢复，例如解析残缺标记后的 <title>。
    std::optional<std::string> Finish();

    /// View the first 1024 input bytes or the shorter prefix consumed so far.
    std::string_view sample() const {
        return bytes_.sample();
    }

    /// Return the first recognized meta charset declaration, or an empty string.
    std::string DeclaredCharset() const {
        return charset_;
    }

    /// Report whether the input began with a UTF-8 or UTF-16 byte order mark.
    bool unicode_bom() const {
        return bytes_.has_bom();
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
    void HandleCompleteTag();

    DocumentBytes bytes_;

    State state_ = State::kText;
    std::string tag_;
    std::string charset_;
    std::optional<std::string> social_title_;
    std::string raw_element_;
    unsigned template_depth_ = 0;
    unsigned svg_depth_ = 0;
    unsigned dashes_ = 0;
    char quote_ = 0;
    std::size_t title_tag_ = std::string::npos;
    std::string title_;
};
} // namespace tb

#endif // TITLEBOT_TITLE_PARSER_HPP_
