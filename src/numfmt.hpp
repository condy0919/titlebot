#include <cstdio>
#include <string>

namespace tb {
std::string numfmt(double sz) {
    const char units[] = " KMGTP";

    char buf[128] = "";
    std::size_t idx = 0;
    while (idx < sizeof(units) - 2 && sz >= 1024) {
        sz /= 1024;
        ++idx;
    }

    int len;
    std::snprintf(buf, sizeof(buf) - 2, "%.2lf%n", sz, &len);
    buf[len++] = units[idx];
    buf[len++] = 'B';
    return {buf, static_cast<std::size_t>(len)};
}
} // namespace tb
