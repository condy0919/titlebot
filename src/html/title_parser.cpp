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
    for (std::sregex_iterator it(meta_tags_.begin(), meta_tags_.end(), meta), end; it != end;
         ++it) {
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
    if (state_ == State::kTitle) {
        if (ch == '>') {
            constexpr std::string_view close = "</title";
            auto end = title_.find_last_not_of(" \t\r\n\f");
            if (end != title_.npos && end + 1 >= close.size()) {
                auto start = end + 1 - close.size();
                if (boost::beast::iequals(std::string_view(title_).substr(start, close.size()),
                                          close)) {
                    title_.resize(start);
                    state_ = State::kText;
                    return std::exchange(title_, {});
                }
            }
        }
        title_.push_back(ch);
        return {};
    }
    if (state_ == State::kComment) {
        if (ch == '>' && dashes_ == 2) {
            state_ = State::kText;
        }
        dashes_ = ch == '-' ? std::min(2u, dashes_ + 1) : 0;
        return {};
    }
    if (ch == '<' && !quote_) {
        tag_.clear();
        state_ = State::kTag;
        return {};
    }
    if (state_ != State::kTag) {
        return {};
    }
    if (ch != '>' || quote_) {
        tag_ += ch;
        if (raw_element_.empty()) {
            if (!quote_ && (ch == '\'' || ch == '"')) {
                quote_ = ch;
            } else if (quote_ == ch) {
                quote_ = 0;
            }
            if (tag_ == "!--") {
                state_ = State::kComment;
                dashes_ = 0;
            }
        }
        return {};
    }
    state_ = State::kText;
    std::string_view tag(tag_);
    auto first = tag.find_first_not_of(" \t\r\n\f");
    if (first == tag.npos) {
        return {};
    }
    tag.remove_prefix(first);
    bool closing = tag.starts_with('/');
    if (closing) {
        tag.remove_prefix(1);
    }
    auto name = tag.substr(0, tag.find_first_of(" \t\r\n\f/"));
    if (!raw_element_.empty()) {
        if (closing && boost::beast::iequals(name, raw_element_)) {
            raw_element_.clear();
        }
    } else if (boost::beast::iequals(name, "svg")) {
        if (closing && svg_depth_) {
            --svg_depth_;
        } else if (!closing && !tag.ends_with('/')) {
            ++svg_depth_;
        }
    } else if (!closing &&
               (boost::beast::iequals(name, "script") || boost::beast::iequals(name, "style"))) {
        raw_element_ = name;
    } else if (!closing && !svg_depth_) {
        if (boost::beast::iequals(name, "title") && !tag.ends_with('/')) {
            state_ = State::kTitle;
        } else if (boost::beast::iequals(name, "meta") && head_.size() < 1024) {
            meta_tags_ += "<" + tag_ + ">";
        }
    }
    return {};
}
} // namespace tb
