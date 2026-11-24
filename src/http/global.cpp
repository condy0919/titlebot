#include "global.hpp"

namespace Global {
ssl::Context& getSSLContext() {
    static ssl::Context cntx;
    return cntx;
}

DNSCache<1800>& getDNSCache() {
    static DNSCache<1800> cache;
    return cache;
}
}
