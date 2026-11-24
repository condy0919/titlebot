#ifndef TITLEBOT_IRC_HPP_
#define TITLEBOT_IRC_HPP_

#include <optional>
#include <string>
#include <string_view>

namespace tb {
/// 返回 rest 中第一个 ASCII 空格之前的字段视图，并从 rest 中移除该字段和紧随其后的所有空格。
/// 若没有空格，则返回整个字段并清空 rest。若以空格开头，则返回空字段并跳过开头的空格。
/// 例如，若 rest 为 `"PRIVMSG  #room :你好"`，则返回 `"PRIVMSG"`，rest 变为 `"#room :你好"`。
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
