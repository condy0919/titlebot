#pragma once

#include "ssl_ctx.hpp"
#include "dns_cache.hpp"

namespace Global {
ssl::Context& getSSLContext();
DNSCache<1800>& getDNSCache();
}
