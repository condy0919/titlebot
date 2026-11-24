#include "title_parser.hpp"
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/beast/core/string.hpp>
#include <boost/beast/http/rfc7230.hpp>
#include <boost/locale/utf.hpp>
#include <cctype>
#include <utility>

namespace tb {

namespace {
std::string MetaCharset(std::string_view attrs) {
    constexpr std::string_view whitespace = " \t\r\n\f";
    auto trim = [&] {
        attrs.remove_prefix(std::min(attrs.size(), attrs.find_first_not_of(whitespace)));
    };
    std::string_view content, equiv;
    auto label = [](std::string_view value) {
        if (value.empty() || value.size() > 64 ||
            value.find_first_not_of(
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._:-") !=
                value.npos) {
            return std::string{};
        }
        return std::string(value);
    };
    while (!attrs.empty()) {
        trim();
        if (attrs.empty()) {
            break;
        }
        auto n = std::min(attrs.size(), attrs.find_first_of(" \t\r\n\f=/"));
        auto key = attrs.substr(0, n);
        attrs.remove_prefix(n);
        trim();
        if (!attrs.starts_with('=')) {
            if (!n && !attrs.empty()) {
                attrs.remove_prefix(1);
            }
            continue;
        }
        attrs.remove_prefix(1);
        trim();
        bool quoted = attrs.starts_with('"') || attrs.starts_with('\'');
        char quote = quoted ? attrs.front() : 0;
        if (quoted) {
            attrs.remove_prefix(1);
        }
        auto end = quoted ? attrs.find(quote) : attrs.find_first_of(whitespace);
        auto value = attrs.substr(0, end);
        attrs.remove_prefix(value.size());
        if (quoted && !attrs.empty()) {
            attrs.remove_prefix(1);
        }
        if (boost::beast::iequals(key, "charset")) {
            auto result = label(value);
            if (!result.empty()) {
                return result;
            }
        } else if (boost::beast::iequals(key, "content")) {
            content = value;
        } else if (boost::beast::iequals(key, "http-equiv")) {
            equiv = value;
        }
    }
    auto semi = content.find(';');
    if (boost::beast::iequals(equiv, "content-type") && semi != content.npos) {
        for (auto [key, value] : boost::beast::http::param_list(content.substr(semi))) {
            if (boost::beast::iequals(key, "charset")) {
                return label(value);
            }
        }
    }
    return {};
}
} // namespace

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
        } else if (boost::beast::iequals(name, "meta") && charset_.empty()) {
            charset_ = MetaCharset(tag.substr(name.size()));
        }
    }
    return {};
}
} // namespace tb
