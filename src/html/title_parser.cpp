#include "title_parser.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

#include <boost/beast/core/string.hpp>
#include <boost/beast/http/rfc7230.hpp>
#include <boost/locale/utf.hpp>

namespace tb {
namespace {
constexpr std::string_view kHtmlWhitespace = " \t\r\n\f";

// Advance the view past leading HTML whitespace, e.g. "  charset=utf-8" becomes "charset=utf-8".
void SkipHtmlWhitespace(std::string_view& attrs) {
    attrs.remove_prefix(std::min(attrs.size(), attrs.find_first_not_of(kHtmlWhitespace)));
}

// Accept up to 64 ASCII label characters and preserve their spelling. For example,
// "Shift_JIS" is returned unchanged, while "Shift JIS" yields an empty string.
std::string ParseCharsetLabel(std::string_view value) {
    if (value.empty() || value.size() > 64 ||
        value.find_first_not_of(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._:-") != value.npos) {
        return {};
    }
    return std::string(value);
}

struct Attribute {
    std::string_view name;
    std::string_view value;
};

// Consume the next name=value pair, skipping valueless attributes and stray slashes.
// For example, "disabled / charset='UTF-8'" yields {"charset", "UTF-8"} and exhausts attrs.
// Both views refer to the original tag.
std::optional<Attribute> NextAttribute(std::string_view& attrs) {
    while (!attrs.empty()) {
        SkipHtmlWhitespace(attrs);
        if (attrs.empty()) {
            break;
        }
        // Read the attribute name. With no delimiter, the whole remaining view is the name.
        const auto n = std::min(attrs.size(), attrs.find_first_of(" \t\r\n\f=/"));
        const auto key = attrs.substr(0, n);
        attrs.remove_prefix(n);
        SkipHtmlWhitespace(attrs);
        if (!attrs.starts_with('=')) {
            // Ignore attributes without values. If a stray '/' stopped the name scan
            // before it consumed anything, step past it so the next iteration advances.
            if (!n && !attrs.empty()) {
                attrs.remove_prefix(1);
            }
            continue;
        }
        attrs.remove_prefix(1);
        SkipHtmlWhitespace(attrs);
        // Quotes allow spaces inside values such as "text/html; charset=utf-8".
        // Unquoted values end at whitespace. If no terminator is found, take the remainder.
        const bool quoted = attrs.starts_with('"') || attrs.starts_with('\'');
        const char quote = quoted ? attrs.front() : 0;
        if (quoted) {
            attrs.remove_prefix(1);
        }
        const auto end = quoted ? attrs.find(quote) : attrs.find_first_of(kHtmlWhitespace);
        auto value = attrs.substr(0, end);
        attrs.remove_prefix(value.size());
        if (quoted && !attrs.empty()) {
            // Skip the matching closing quote before scanning the next attribute.
            attrs.remove_prefix(1);
        }
        if (!quoted && attrs.empty() && value.ends_with('/')) {
            value.remove_suffix(1);
        }
        return Attribute{key, value};
    }
    return std::nullopt;
}

// Extract the encoding label declared by one <meta> tag. The caller passes only the
// attributes: MetaCharset(" charset='UTF-8'") returns "UTF-8". An empty string means
// this tag supplied no usable declaration, so later <meta> tags can still be checked.
std::string MetaCharset(std::string_view attrs) {
    // The legacy form needs both attributes. Keep views into the tag so they can appear
    // in either order, e.g. content='text/html; charset=shift_jis' before http-equiv.
    std::string_view content, equiv;
    while (auto attr = NextAttribute(attrs)) {
        // Attribute names are case-insensitive. A valid direct charset takes precedence
        // over the legacy form, even if content and http-equiv were seen first.
        if (boost::beast::iequals(attr->name, "charset")) {
            auto result = ParseCharsetLabel(attr->value);
            if (!result.empty()) {
                return result;
            }
        } else if (boost::beast::iequals(attr->name, "content")) {
            content = attr->value;
        } else if (boost::beast::iequals(attr->name, "http-equiv")) {
            equiv = attr->value;
        }
    }
    // Interpret content as a MIME type with parameters only for http-equiv="Content-Type".
    // Start at the semicolon so param_list sees "; charset=shift_jis" after "text/html".
    const auto semi = content.find(';');
    if (boost::beast::iequals(equiv, "content-type") && semi != content.npos) {
        for (auto [key, value] : boost::beast::http::param_list(content.substr(semi))) {
            if (boost::beast::iequals(key, "charset")) {
                return ParseCharsetLabel(value);
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
        if (ch == '<') {
            title_tag_ = title_.size();
        } else if (ch == '>' && title_tag_ != title_.npos) {
            auto tag = std::string_view(title_).substr(title_tag_ + 1);
            if (tag.starts_with('/') &&
                boost::beast::iequals(tag.substr(1, tag.find_first_of(" /\t\r\n\f", 1) - 1), "title")) {
                title_.resize(title_tag_);
                state_ = State::kText;
                title_tag_ = title_.npos;
                return std::exchange(title_, {});
            }
            title_tag_ = title_.npos;
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
    if (ch == '<' && (!quote_ || tag_.size() >= 16384)) {
        quote_ = 0;
        tag_.clear();
        state_ = State::kTag;
        return {};
    }
    if (state_ != State::kTag) {
        return {};
    }
    if (ch != '>' || quote_) {
        if (tag_.empty()) {
            if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '/' || ch == '!' ||
                  ch == '?')) {
                state_ = State::kText;
                return {};
            }
        }
        if (raw_element_.empty()) {
            if (quote_ && quote_ == ch) {
                quote_ = 0;
            } else if (!quote_ && (ch == '\'' || ch == '"')) {
                auto last = tag_.find_last_not_of(" \t\r\n\f");
                if (last != tag_.npos && tag_[last] == '=') {
                    quote_ = ch;
                }
            }
        }
        tag_ += ch;
        if (raw_element_.empty() && tag_ == "!--") {
            state_ = State::kComment;
            // Count the two opening dashes so a short comment can close immediately.
            dashes_ = 2;
        }
        return {};
    }
    HandleCompleteTag();
    return {};
}

void TitleParser::HandleCompleteTag() {
    state_ = State::kText;
    std::string_view tag(tag_);
    auto first = tag.find_first_not_of(" \t\r\n\f");
    if (first == tag.npos) {
        return;
    }
    tag = tag.substr(first, tag.find_last_not_of(" \t\r\n\f") - first + 1);
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
               (boost::beast::iequals(name, "script") || boost::beast::iequals(name, "style") ||
                boost::beast::iequals(name, "textarea") || boost::beast::iequals(name, "xmp") ||
                boost::beast::iequals(name, "noframes") || boost::beast::iequals(name, "noembed"))) {
        raw_element_ = name;
    } else if (!svg_depth_ && boost::beast::iequals(name, "template")) {
        if (!closing) {
            ++template_depth_;
        } else if (template_depth_) {
            --template_depth_;
        }
    } else if (!closing && !svg_depth_ && !template_depth_) {
        if (boost::beast::iequals(name, "title") && !tag.ends_with('/')) {
            state_ = State::kTitle;
        } else if (boost::beast::iequals(name, "meta") && charset_.empty()) {
            charset_ = MetaCharset(tag.substr(name.size()));
        }
    }
}
} // namespace tb
