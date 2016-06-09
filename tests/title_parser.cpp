#define CATCH_CONFIG_MAIN
#include "../src/title_parser.hpp"
#include <catch.hpp>
#include <string>

TEST_CASE("title parser", "[title parser]") {
    TitleParser tp;
    std::string s;

    SECTION("parse normal title") {
        s = "fff <title> xxx </title> aaa";
        bool b = tp.parse(s.begin(), s.end());
        REQUIRE(b);
    }
    SECTION("there is no title") {
        s = "<head> xx </head>";
        bool b = tp.parse(s.begin(), s.end());
        REQUIRE(!b);
    }
    SECTION("parse title with property") {
        s = "<title itemprop=\"name\">twitter</title>";
        bool b = tp.parse(s.begin(), s.end());
        REQUIRE(b);
    }
    SECTION("< title >") {
        s = "<    title  >twitter</title>";
        bool b = tp.parse(s.begin(), s.end());
        REQUIRE(b);
    }
}
