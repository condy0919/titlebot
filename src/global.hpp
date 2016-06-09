#pragma once

#include "ssl_ctx.hpp"

namespace Global {
ssl::Context& getSSLContext();
}
