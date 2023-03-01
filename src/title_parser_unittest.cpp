#include "title_parser.hpp"

#include <doctest/doctest.h>

TEST_CASE("<tilte>This is title</tilte>") {
    const std::string s = "before <title>this is title</title> after";
    tb::TitleParser parser;
    REQUIRE_EQ(parser.parse(s.begin(), s.end()).value(), "this is title");
}

TEST_CASE("<head>nothing</head>") {
    const std::string s = "<head>nothing</head>";
    tb::TitleParser parser;
    REQUIRE_FALSE(parser.parse(s.begin(), s.end()).has_value());
}

TEST_CASE("tilte with properties") {
    const std::string s = "<title itemprop=\"name\">twitter</title>";
    tb::TitleParser parser;
    REQUIRE_EQ(parser.parse(s.begin(), s.end()).value(), "twitter");
}

TEST_CASE("< title >") {
    const std::string s = "<  title >facebook</title>";
    tb::TitleParser parser;
    REQUIRE_EQ(parser.parse(s.begin(), s.end()).value(), "facebook");
}
