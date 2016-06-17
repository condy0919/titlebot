#include "format.hpp"
#include <boost/lexical_cast.hpp>
#include <cstdio>

std::string numfmt(double sz) {
    static const char units[] = " KMGTPEZY";
    char buf[10] = "";
    int len;
    std::size_t idx = 0;
    while (idx < sizeof(units) - 2 && sz >= 1024) {
        sz /= 1024;
        ++idx;
    }
    std::sprintf(buf, "%.2lf%n", sz, &len);
    buf[len++] = units[idx];
    buf[len++] = 'B';
    return std::string(buf, len);
}

std::string numfmt(std::string sz) {
    return numfmt(sz.empty() ? 0 : boost::lexical_cast<double>(sz));
}
