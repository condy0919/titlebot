#include "numfmt.hpp"

#include <gtest/gtest.h>

TEST(Numfmt, FormatsByteCounts) {
    ASSERT_EQ(tb::numfmt(1024), "1.00KB");
    ASSERT_EQ(tb::numfmt(1048576), "1.00MB");
    ASSERT_EQ(tb::numfmt(213123), "208.13KB");
}

TEST(Numfmt, HandlesLargeMagnitudes) {
    EXPECT_TRUE(tb::numfmt(1e300).ends_with("PB"));
}
