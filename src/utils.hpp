#ifndef TITLEBOT_UTILS_HPP_
#define TITLEBOT_UTILS_HPP_

#include <string>
#include <optional>

namespace tb {
/// unquote http stuff
std::string unquote(const char* s);

/// Convert `in` of any other encoding to utf-8.
std::optional<std::string> utf8(const std::string& in);
}

#endif // TITLEBOT_UTILS_HPP_
