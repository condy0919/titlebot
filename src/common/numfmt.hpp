#ifndef TITLEBOT_NUMFMT_HPP_
#define TITLEBOT_NUMFMT_HPP_
#include <format>
#include <string>

namespace tb {
inline std::string numfmt(double sz) {
    const char units[] = " KMGTP";

    std::size_t idx = 0;
    while (idx < sizeof(units) - 2 && sz >= 1024) {
        sz /= 1024;
        ++idx;
    }

    return std::format("{:.2f}{}B", sz, units[idx]);
}
} // namespace tb

#endif
