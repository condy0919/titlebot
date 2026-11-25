#ifndef TITLEBOT_IRC_HPP_
#define TITLEBOT_IRC_HPP_

#include <optional>
#include <string>
#include <string_view>

namespace tb {
struct PrivMsg {
    std::string who;
    std::string channel;
    std::string protocol;
    std::string url;
};

/// Fold one nickname or channel byte using the RFC 1459 casemapping.
/// For example, FoldIrcCase('A') returns 'a', and FoldIrcCase('[') returns '{'.
char FoldIrcCase(char c);

/// Return a view of the field before the first ASCII space in rest. Remove the field and any
/// following spaces from rest. A leading space yields an empty field. With no space, consume all of
/// rest.
/// For example, given `"PRIVMSG  #room :hello"`, return `"PRIVMSG"` and leave `"#room :hello"` in
/// rest.
std::string_view TakeIrcToken(std::string_view& rest);

/// Strip IRC color sequences and control bytes, preserving tabs and printable text.
/// For example, `normalize("\00304red")` returns `"red"` (color 04 followed by text).
std::string normalize(std::string_view s);

/// Extract the first HTTP or HTTPS link from a PRIVMSG. Return std::nullopt for an
/// unhandled line. For example, parse_privmsg(":a!u@h PRIVMSG #r :https://one/ http://two/")
/// returns a PrivMsg with who="a", channel="#r", protocol="https", and url="one/".
std::optional<PrivMsg> parse_privmsg(std::string_view s);
} // namespace tb

#endif // TITLEBOT_IRC_HPP_
