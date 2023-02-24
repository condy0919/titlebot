#include "utils.hpp"

#include "doctest/doctest.h"

TEST_CASE("unquote") {
    REQUIRE_EQ(tb::unquote("&amp;"), "&");
    REQUIRE_EQ(tb::unquote("&lt;"), "<");
    REQUIRE_EQ(tb::unquote("&mu;"), "μ");
    REQUIRE_EQ(tb::unquote("template&lt;T&gt; foo"), "template<T> foo");
}
