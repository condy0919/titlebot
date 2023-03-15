#ifndef TITLEBOT_IRC_HPP_
#define TITLEBOT_IRC_HPP_

#include <optional>
#include <string>

namespace tb {
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
