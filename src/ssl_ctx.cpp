#include "ssl_ctx.hpp"

namespace ssl {
Context::Context() : ctx_(boost::asio::ssl::context::sslv23) {
    ctx_.set_default_verify_paths();
}

boost::asio::ssl::context& Context::native() {
    return ctx_;
}
}
