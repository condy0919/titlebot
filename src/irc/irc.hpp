#ifndef TITLEBOT_IRC_HPP_
#define TITLEBOT_IRC_HPP_

#include <optional>
#include <string>
#include <string_view>

namespace tb {
/// Return a view of the field before the first ASCII space in rest. Remove the field and any
/// following spaces from rest. A leading space yields an empty field. With no space, consume all of
/// rest.
/// For example, given `"PRIVMSG  #room :hello"`, return `"PRIVMSG"` and leave `"#room :hello"` in
/// rest.
std::string_view TakeIrcToken(std::string_view& rest);

struct PrivMsg {
    std::string who;
    std::string channel;
    std::string protocol;
    std::string url;
};

/// Remove colors from IRC texts.
std::string normalize(const std::string& s);

/// Parse IRC private message.
///
/// e.g.,
///
/// ```text
/// :condy!~condy@unaffiliated/condy PRIVMSG #linuxba :http://www.baidu.com
/// ```
std::optional<PrivMsg> parse_privmsg(const std::string& s);
} // namespace tb

#endif // TITLEBOT_IRC_HPP_
