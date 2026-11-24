#include "title_parser.hpp"
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/beast/core/string.hpp>
#include <boost/locale/utf.hpp>
#include <cctype>
#include <regex>
#include <utility>

namespace tb {

std::string TitleParser::DeclaredCharset() const {
    static const std::regex meta(R"(<meta\s+([^<>]*)>)", std::regex::icase);
    static const std::regex attribute(
        R"attr(([\w:-]+)\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s"'<>]+)))attr");
    static const std::regex charset(R"(charset\s*=\s*([\w.-]+))", std::regex::icase);
    for (std::sregex_iterator it(head_.begin(), head_.end(), meta), end; it != end; ++it) {
        auto attrs = (*it)[1].str();
        std::string content, equiv;
        for (std::sregex_iterator a(attrs.begin(), attrs.end(), attribute); a != end; ++a) {
            auto name = boost::algorithm::to_lower_copy((*a)[1].str());
            auto value = (*a)[2].matched   ? (*a)[2].str()
                         : (*a)[3].matched ? (*a)[3].str()
                                           : (*a)[4].str();
            if (name == "charset") {
                return value;
            }
            if (name == "http-equiv") {
                equiv = value;
            }
            if (name == "content") {
                content = value;
            }
        }
        std::smatch match;
        if (boost::beast::iequals(equiv, "content-type") &&
            std::regex_search(content, match, charset)) {
            return match[1].str();
        }
    }
    return {};
}

std::optional<std::string> TitleParser::ReadByte(char ch) {
    if (head_.size() < 1024) {
        head_.push_back(ch);
        if (head_.size() == 2 && (head_ == "\xff\xfe" || head_ == "\xfe\xff")) {
            utf16_ = head_[0] == '\xff' ? 1 : 2;
            return {};
        }
    }
    if (!utf16_) {
        return consume(ch);
    }
    auto byte = static_cast<unsigned char>(ch);
    if (first_byte_ < 0) {
        first_byte_ = byte;
        return {};
    }
    unsigned unit = utf16_ == 1 ? first_byte_ | (byte << 8) : (first_byte_ << 8) | byte;
    first_byte_ = -1;
    if (unit >= 0xd800 && unit <= 0xdbff) {
        surrogate_ = unit;
        return {};
    }
    if (surrogate_ && unit >= 0xdc00 && unit <= 0xdfff) {
        unit = 0x10000 + ((surrogate_ - 0xd800) << 10) + unit - 0xdc00;
    } else if (unit >= 0xdc00 && unit <= 0xdfff) {
        unit = 0xfffd;
    }
    surrogate_ = 0;
    char bytes[4];
    auto end = boost::locale::utf::utf_traits<char>::encode(unit, bytes);
    for (auto p = bytes; p != end; ++p) {
        if (auto title = consume(*p)) {
            return title;
        }
    }
    return {};
}

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
