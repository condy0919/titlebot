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

bool BlankTitle(std::string_view value) {
    return std::ranges::all_of(value, [](unsigned char c) { return c <= 32 || c == 127; });
}

std::string LegacyCharset(std::string_view equiv, std::string_view content) {
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
    for (char decoded : bytes_.Push(ch)) {
        if (auto title = consume(decoded)) {
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
                boost::beast::iequals(tag.substr(1, tag.find_first_of(" /\t\r\n\f", 1) - 1),
                                      "title")) {
                title_.resize(title_tag_);
                state_ = State::kText;
                title_tag_ = title_.npos;
                auto title = std::exchange(title_, {});
                if (BlankTitle(title)) {
                    return {};
                }
                return title;
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
    if (ch == '<' && !quote_) {
        quote_ = 0;
        tag_.clear();
        state_ = State::kTag;
        return {};
    }
    if (state_ != State::kTag) {
        return {};
    }
    if (!raw_element_.empty() && tag_.empty() && ch != '/') {
        state_ = State::kText;
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

std::optional<std::string> TitleParser::Finish() {
    if (state_ == State::kTag && quote_) {
        // Only EOF distinguishes an unfinished attribute from quoted markup. Replay its tail once.
        auto remainder = std::exchange(tag_, {});
        state_ = State::kText;
        quote_ = 0;
        auto first = remainder.find('<');
        if (first != remainder.npos) {
            for (char ch : std::string_view(remainder).substr(first)) {
                if (auto title = consume(ch)) {
                    return title;
                }
            }
        }
    }
    return std::exchange(social_title_, std::nullopt);
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
                boost::beast::iequals(name, "noframes") ||
                boost::beast::iequals(name, "noembed"))) {
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
        } else if (boost::beast::iequals(name, "meta")) {
            auto attrs = tag.substr(name.size());
            std::string_view property, content, equiv;
            while (auto attr = NextAttribute(attrs)) {
                if (boost::beast::iequals(attr->name, "charset") && charset_.empty()) {
                    charset_ = ParseCharsetLabel(attr->value);
                } else if (boost::beast::iequals(attr->name, "property")) {
                    property = attr->value;
                } else if (boost::beast::iequals(attr->name, "content")) {
                    content = attr->value;
                } else if (boost::beast::iequals(attr->name, "http-equiv")) {
                    equiv = attr->value;
                }
            }
            if (charset_.empty()) {
                charset_ = LegacyCharset(equiv, content);
            }
            if (!social_title_ && boost::beast::iequals(property, "og:title") &&
                !BlankTitle(content)) {
                social_title_ = content;
            }
        }
    }
}
} // namespace tb
