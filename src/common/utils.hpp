#ifndef TITLEBOT_UTILS_HPP_
#define TITLEBOT_UTILS_HPP_
#include <optional>
#include <string>
#include <string_view>

namespace tb {
std::string unquote(std::string_view text);
std::optional<std::string> utf8(const std::string& in, const std::string& charset = {},
                                std::string_view sample = {});
} // namespace tb
#endif
