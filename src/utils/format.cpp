#include "format.hpp"
#include <boost/lexical_cast.hpp>

std::string numfmt(double sz) {
    static const char units[] = " KMGTPEZY";
    std::size_t idx = 0;
    while (idx < sizeof(units) - 2 && sz >= 1024) {
        sz /= 1024;
        ++idx;
    }
    std::string ret = boost::lexical_cast<std::string>(sz);
    ret += units[idx];
    ret += 'B';
    return ret;
}

std::string numfmt(std::string sz) {
    return numfmt(sz.empty() ? 0 : boost::lexical_cast<double>(sz));
}
