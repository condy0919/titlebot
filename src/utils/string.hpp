#pragma once

#include <string>

namespace StringUtils {
void concat(std::string&);

template <typename T, typename... Ts,
          std::enable_if_t<std::is_same<std::common_type_t<std::string, T>,
                                        std::string>::value,
                           int> = 0>
void concat(std::string& s, T&& t, Ts&&... ts) {
    s += std::forward<T>(t);
    concat(s, std::forward<Ts>(ts)...);
}
}
