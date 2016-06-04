#include "global.hpp"

namespace Global {
ssl::Context& getSSLContext() {
    static ssl::Context cntx;
    return cntx;
}
}
