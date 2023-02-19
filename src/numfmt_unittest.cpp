#include "numfmt.hpp"

#include "doctest/doctest.h"

TEST_CASE("Numfmt") {
    REQUIRE_EQ(tb::numfmt(1024), "1.00KB");
    REQUIRE_EQ(tb::numfmt(1048576), "1.00MB");
    REQUIRE_EQ(tb::numfmt(213123), "208.13KB");
}
