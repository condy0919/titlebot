#define CATCH_CONFIG_MAIN
#include "../src/utils/format.hpp"
#include <catch.hpp>

TEST_CASE("number format", "[number format]") {
    std::string s;

    SECTION("format 1024 string") {
        s = numfmt("1024");
        REQUIRE(s == "1.00KB");
    }
    SECTION("format 1024 float") {
        s = numfmt(1024);
        REQUIRE(s == "1.00KB");
    }
    SECTION("format 1048576") {
        s = numfmt(1048576);
        REQUIRE(s == "1.00MB");
    }
    SECTION("format 213123") {
        s = numfmt(213123);
        REQUIRE(s == "208.13KB");
    }
}
