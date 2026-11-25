#ifndef IRCBOT_MESSAGE_HPP_
#define IRCBOT_MESSAGE_HPP_

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <boost/container/small_vector.hpp>

namespace ircbot {
enum class ParseError {
    kEmpty,
    kInvalidCharacter,
    kMissingCommand,
    kInvalidCommand,
    kInvalidPrefix,
    kInvalidTags,
    kLineTooLong,
};

struct MessageView {
    std::string_view tags;
    std::string_view prefix;
    std::string_view command;
    boost::container::small_vector<std::string_view, 15> params;

    /// For prefix "alice!user@host", return "alice"; a missing prefix yields an empty view.
    std::string_view Nick() const;
    /// For "nick!alice@host", User() returns "alice".
    std::string_view User() const;
    /// For "nick@irc.example", Host() returns "irc.example".
    std::string_view Host() const;
    /// Decode the last matching tag. For tags "label=one;label=two\\swords", Tag("label")
    /// returns "two words". Missing tags and values containing invalid UTF-8 yield nullopt.
    std::optional<std::string> Tag(std::string_view key) const;
};

/// Parse one line without CRLF, retaining views into line. For example,
/// ParseMessage(":a!u@h PRIVMSG #r :hello world") yields params {"#r", "hello world"}.
/// The caller must keep line alive and unchanged for the lifetime of the returned views.
std::expected<MessageView, ParseError> ParseMessage(std::string_view line);

struct TextMessageView {
    std::string_view sender;
    std::string_view target;
    std::string_view text;
};

/// For example, ":bot!u@h PRIVMSG #room :hello" yields {"bot", "#room", "hello"}.
std::optional<TextMessageView> AsPrivmsg(const MessageView& message);
/// For example, ":server NOTICE bot :welcome" yields {"server", "bot", "welcome"}.
std::optional<TextMessageView> AsNotice(const MessageView& message);

/// Compare command names using ASCII case folding, e.g. IsCommand(parsed_ping, "PING") is true
/// when parsed_ping.command is "pInG". Numeric commands compare byte for byte.
bool IsCommand(const MessageView& message, std::string_view command);

/// Relay budget for PRIVMSG including a server prefix, e.g. ("#r", 8) reserves
/// ":nick!u@h " and leaves 488 text bytes. Supply the full nick!user@host byte length.
std::size_t PrivmsgTextBudget(std::string_view target, std::size_t prefix_bytes);

struct Tag {
    std::string key;
    std::string value;
};

enum class SerializeError {
    kInvalidCommand,
    kInvalidParameter,
    kInvalidTag,
    kTooManyParameters,
    kLineTooLong,
};

/// Split valid UTF-8 without copying bytes. For example, ("a猫b", 3) yields {"a", "猫", "b"}.
/// The input must outlive all returned views.
std::expected<std::vector<std::string_view>, SerializeError>
SplitText(std::string_view text, std::size_t budget);

/// Serialize a client message without CRLF. For example, ("PRIVMSG", {"#r", "hello world"})
/// yields "PRIVMSG #r :hello world". Reject invalid fields and separately bound tags and body.
std::expected<std::string, SerializeError>
SerializeMessage(std::string_view command, std::span<const std::string_view> params,
                 std::span<const Tag> tags = {}, bool force_trailing = false);
} // namespace ircbot

#endif // IRCBOT_MESSAGE_HPP_
