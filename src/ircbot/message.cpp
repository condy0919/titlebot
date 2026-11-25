#include "ircbot/message.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace ircbot {
namespace {
constexpr std::size_t kBodyLimit = 510;
constexpr std::size_t kIncomingTagLimit = 8191;
constexpr std::size_t kOutgoingTagDataLimit = 4094;

char Lower(char ch) {
    return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + ('a' - 'A')) : ch;
}

bool IsLetter(char ch) {
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z');
}

bool IsDigit(char ch) {
    return ch >= '0' && ch <= '9';
}

bool ValidCommand(std::string_view text) {
    return !text.empty() && (std::ranges::all_of(text, IsLetter) ||
                             (text.size() == 3 && std::ranges::all_of(text, IsDigit)));
}

bool HasLineBreakOrNul(std::string_view text) {
    return text.find_first_of(std::string_view("\0\r\n", 3)) != text.npos;
}

void SkipSpaces(std::string_view& text) {
    const auto first = text.find_first_not_of(' ');
    text.remove_prefix(first == text.npos ? text.size() : first);
}

std::string_view TakeWord(std::string_view& text) {
    const auto word = text.substr(0, text.find(' '));
    text.remove_prefix(word.size());
    SkipSpaces(text);
    return word;
}

bool ValidUtf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i++]);
        if (lead < 0x80) {
            continue;
        }
        unsigned count;
        unsigned point;
        unsigned minimum;
        if (lead >= 0xc2 && lead <= 0xdf) {
            count = 1;
            point = lead & 0x1f;
            minimum = 0x80;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            count = 2;
            point = lead & 0xf;
            minimum = 0x800;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            count = 3;
            point = lead & 0x7;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (text.size() - i < count) {
            return false;
        }
        while (count--) {
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) {
                return false;
            }
            point = (point << 6) | (next & 0x3f);
        }
        if (point < minimum || point > 0x10ffff || (point >= 0xd800 && point <= 0xdfff)) {
            return false;
        }
    }
    return true;
}

bool ValidTagKey(std::string_view key) {
    if (key.starts_with('+')) {
        key.remove_prefix(1);
    }
    const auto slash = key.find('/');
    if (slash != key.npos) {
        const auto vendor = key.substr(0, slash);
        if (vendor.empty() || !std::ranges::all_of(vendor, [](char ch) {
                return IsLetter(ch) || IsDigit(ch) || ch == '-' || ch == '.';
            })) {
            return false;
        }
        key.remove_prefix(slash + 1);
    }
    return !key.empty() && std::ranges::all_of(key, [](char ch) {
        return IsLetter(ch) || IsDigit(ch) || ch == '-';
    });
}

char Escape(char ch) {
    switch (ch) {
    case ';':
        return ':';
    case ' ':
        return 's';
    case '\\':
        return '\\';
    case '\r':
        return 'r';
    case '\n':
        return 'n';
    default:
        return 0;
    }
}
std::optional<std::string> DecodeTagValue(std::string_view value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        auto ch = value[i];
        if (ch == '\\') {
            if (++i == value.size()) {
                break;
            }
            switch (ch = value[i]) {
            case ':':
                ch = ';';
                break;
            case 's':
                ch = ' ';
                break;
            case 'r':
                ch = '\r';
                break;
            case 'n':
                ch = '\n';
                break;
            }
        }
        decoded += ch;
    }
    return ValidUtf8(decoded) ? std::optional(std::move(decoded)) : std::nullopt;
}
} // namespace

std::string_view MessageView::Nick() const {
    return prefix.substr(0, prefix.find_first_of("!@"));
}

std::string_view MessageView::User() const {
    const auto separator = prefix.find_first_of("!@");
    if (separator == prefix.npos || prefix[separator] != '!') {
        return {};
    }
    const auto rest = prefix.substr(separator + 1);
    return rest.substr(0, rest.find('@'));
}

std::string_view MessageView::Host() const {
    const auto at = prefix.find('@');
    return at == prefix.npos ? std::string_view{} : prefix.substr(at + 1);
}

std::optional<std::string> MessageView::Tag(std::string_view key) const {
    std::optional<std::string_view> value;
    auto remaining = tags;
    while (!remaining.empty()) {
        const auto end = remaining.find(';');
        const auto item = remaining.substr(0, end);
        const auto equal = item.find('=');
        if (item.substr(0, equal) == key) {
            value = equal == item.npos ? std::string_view{} : item.substr(equal + 1);
        }
        remaining.remove_prefix(end == remaining.npos ? remaining.size() : end + 1);
    }
    if (!value) {
        return {};
    }
    return DecodeTagValue(*value);
}

std::expected<MessageView, ParseError> ParseMessage(std::string_view line) {
    if (line.empty()) {
        return std::unexpected(ParseError::kEmpty);
    }
    if (line.size() > kBodyLimit + kIncomingTagLimit) {
        return std::unexpected(ParseError::kLineTooLong);
    }
    if (HasLineBreakOrNul(line)) {
        return std::unexpected(ParseError::kInvalidCharacter);
    }
    MessageView message;
    if (line.starts_with('@')) {
        const auto space = line.find(' ');
        if (space == line.npos || space == 1) {
            return std::unexpected(ParseError::kInvalidTags);
        }
        if (space + 1 > kIncomingTagLimit) {
            return std::unexpected(ParseError::kLineTooLong);
        }
        message.tags = line.substr(1, space - 1);
        line.remove_prefix(space + 1);
    }
    if (line.size() > kBodyLimit) {
        return std::unexpected(ParseError::kLineTooLong);
    }
    if (!message.tags.empty()) {
        SkipSpaces(line);
    }
    if (line.starts_with(':')) {
        message.prefix = TakeWord(line).substr(1);
        if (message.prefix.empty()) {
            return std::unexpected(ParseError::kInvalidPrefix);
        }
    }
    if (line.empty()) {
        return std::unexpected(ParseError::kMissingCommand);
    }
    message.command = TakeWord(line);
    if (!ValidCommand(message.command)) {
        return std::unexpected(ParseError::kInvalidCommand);
    }
    while (!line.empty()) {
        if (line.starts_with(':')) {
            message.params.push_back(line.substr(1));
            break;
        }
        message.params.push_back(TakeWord(line));
    }
    return message;
}

namespace {
std::optional<TextMessageView> AsTextMessage(const MessageView& message, std::string_view command) {
    if (!IsCommand(message, command) || message.params.size() != 2) {
        return {};
    }
    return TextMessageView{message.Nick(), message.params[0], message.params[1]};
}
} // namespace

std::optional<TextMessageView> AsPrivmsg(const MessageView& message) {
    return AsTextMessage(message, "PRIVMSG");
}

std::optional<TextMessageView> AsNotice(const MessageView& message) {
    return AsTextMessage(message, "NOTICE");
}

std::size_t PrivmsgTextBudget(std::string_view target, std::size_t prefix_bytes) {
    // "PRIVMSG " and " :" add ten bytes; ":" and " " surround the relay prefix.
    constexpr auto available = kBodyLimit - 12;
    if (target.size() >= available || prefix_bytes >= available - target.size()) {
        return 0;
    }
    return available - target.size() - prefix_bytes;
}

std::expected<std::vector<std::string_view>, SerializeError>
SplitText(std::string_view text, std::size_t budget) {
    if (HasLineBreakOrNul(text) || !ValidUtf8(text)) {
        return std::unexpected(SerializeError::kInvalidParameter);
    }
    std::vector<std::string_view> parts;
    while (text.size() > budget) {
        auto count = budget;
        while (count && (static_cast<unsigned char>(text[count]) & 0xc0) == 0x80) {
            --count;
        }
        if (!count) {
            return std::unexpected(SerializeError::kLineTooLong);
        }
        parts.push_back(text.substr(0, count));
        text.remove_prefix(count);
    }
    parts.push_back(text);
    return parts;
}

std::optional<MembershipView> AsMembershipChange(const MessageView& message) {
    const auto& params = message.params;
    if (params.empty() || params.front().empty()) {
        return {};
    }
    const auto actor = message.Nick();
    if (IsCommand(message, "JOIN")) {
        return MembershipView{MembershipChange::kJoin, actor, params.front(), actor, {}};
    }
    if (IsCommand(message, "PART")) {
        return MembershipView{MembershipChange::kPart, actor, params.front(), actor,
                              params.size() > 1 ? params[1] : std::string_view{}};
    }
    if (IsCommand(message, "KICK") && params.size() > 1) {
        return MembershipView{MembershipChange::kKick, actor, params.front(), params[1],
                              params.size() > 2 ? params[2] : std::string_view{}};
    }
    if (IsCommand(message, "NICK")) {
        return MembershipView{MembershipChange::kNick, actor, {}, params.front(), {}};
    }
    return {};
}

bool IsCommand(const MessageView& message, std::string_view command) {
    return std::ranges::equal(message.command, command, {}, Lower, Lower);
}

std::expected<void, SerializeError> ValidateClientTags(const MessageView& message) {
    if (message.tags.size() > kOutgoingTagDataLimit || message.tags.ends_with(';')) {
        return std::unexpected(SerializeError::kInvalidTag);
    }
    boost::container::small_vector<std::string_view, 8> keys;
    for (auto item : message.RawTags()) {
        const auto equal = item.find('=');
        const auto key = item.substr(0, equal);
        if (!ValidTagKey(key)) {
            return std::unexpected(SerializeError::kInvalidTag);
        }
        if (equal != item.npos) {
            const auto value = item.substr(equal + 1);
            if (value.contains('\\') ? !DecodeTagValue(value).has_value() : !ValidUtf8(value)) {
                return std::unexpected(SerializeError::kInvalidTag);
            }
        }
        keys.push_back(key);
    }
    std::ranges::sort(keys);
    if (std::adjacent_find(keys.begin(), keys.end()) != keys.end()) {
        return std::unexpected(SerializeError::kInvalidTag);
    }
    return {};
}

std::expected<std::string, SerializeError>
SerializeMessage(std::string_view command, std::span<const std::string_view> params,
                 std::span<const Tag> tags, bool force_trailing) {
    if (!ValidCommand(command)) {
        return std::unexpected(SerializeError::kInvalidCommand);
    }
    if (params.size() > 15) {
        return std::unexpected(SerializeError::kTooManyParameters);
    }
    std::size_t body_size = command.size();
    if (body_size > kBodyLimit) {
        return std::unexpected(SerializeError::kLineTooLong);
    }
    bool trailing = false;
    for (std::size_t i = 0; i < params.size(); ++i) {
        const auto param = params[i];
        if (HasLineBreakOrNul(param)) {
            return std::unexpected(SerializeError::kInvalidParameter);
        }
        const bool needs_colon = (force_trailing && i + 1 == params.size()) || param.empty() || param.starts_with(':') || param.contains(' ');
        if (i + 1 != params.size() && needs_colon) {
            return std::unexpected(SerializeError::kInvalidParameter);
        }
        if (param.size() > kBodyLimit || body_size + 1 + needs_colon + param.size() > kBodyLimit) {
            return std::unexpected(SerializeError::kLineTooLong);
        }
        body_size += 1 + needs_colon + param.size();
        trailing = needs_colon;
    }
    std::size_t tag_size = 0;
    for (std::size_t i = 0; i < tags.size(); ++i) {
        const auto& tag = tags[i];
        if (tag.key.size() > kOutgoingTagDataLimit || tag.value.size() > kOutgoingTagDataLimit) {
            return std::unexpected(SerializeError::kLineTooLong);
        }
        if (!ValidTagKey(tag.key) || tag.value.contains('\0') || !ValidUtf8(tag.value)) {
            return std::unexpected(SerializeError::kInvalidTag);
        }
        for (std::size_t j = 0; j < i; ++j) {
            if (tags[j].key == tag.key) {
                return std::unexpected(SerializeError::kInvalidTag);
            }
        }
        tag_size += (i != 0) + tag.key.size();
        if (!tag.value.empty()) {
            tag_size += 1 + tag.value.size();
            tag_size += std::ranges::count_if(tag.value, [](char ch) { return Escape(ch) != 0; });
        }
        if (tag_size > kOutgoingTagDataLimit) {
            return std::unexpected(SerializeError::kLineTooLong);
        }
    }
    std::string result;
    result.reserve(body_size + (tags.empty() ? 0 : tag_size + 2));
    if (!tags.empty()) {
        result += '@';
        for (std::size_t i = 0; i < tags.size(); ++i) {
            if (i) {
                result += ';';
            }
            result += tags[i].key;
            if (!tags[i].value.empty()) {
                result += '=';
                for (char ch : tags[i].value) {
                    if (char escaped = Escape(ch)) {
                        result += '\\';
                        result += escaped;
                    } else {
                        result += ch;
                    }
                }
            }
        }
        result += ' ';
    }
    result += command;
    for (std::size_t i = 0; i < params.size(); ++i) {
        result += ' ';
        if (i + 1 == params.size() && trailing) {
            result += ':';
        }
        result += params[i];
    }
    return result;
}
} // namespace ircbot
