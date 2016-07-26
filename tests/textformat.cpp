#define CATCH_CONFIG_MAIN
#include "../src/textformat.hpp"
#include <catch.hpp>
#include <string>


TEST_CASE("textformat", "[textformat]") {
    std::string s, r;

    SECTION("fg") {
        // \x0301hello
        s = "\x03\x30\x31hello";
        r = IRC::formatNormalize(s);
        REQUIRE(r == "hello");
    }
    SECTION("fg,") {
        // \x0301,
        s = "\x03\x30\x31,";
        r = IRC::formatNormalize(s);
        REQUIRE(r == ",");
    }
    SECTION("fg,illegal") {
        // \x0301,hello
        s = "\x03\x30\x31,hello";
        r = IRC::formatNormalize(s);
        REQUIRE(r == ",hello");
    }
    SECTION("fg,bg") {
        // \x0301,20
        s = "\x03\x30\x31,20";
        r = IRC::formatNormalize(s);
        REQUIRE(r == "0");
    }
}
