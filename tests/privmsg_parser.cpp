#define CATCH_CONFIG_MAIN
#include "../mobot.hpp"
#include <catch.hpp>
#include <iostream>
#include <string>
#include <tuple>


TEST_CASE("PRIVMSG Parser", "[PRIVMSG Parser]") {
    std::string s;
    std::string from, target, url;
    SECTION("a single url") {
        s = ":condy!~condy@unaffiliated/condy PRIVMSG #linuxba :http://www.baidu.com";
        REQUIRE(parse_privmsg(s, from, target, url));
        REQUIRE(from == "condy");
        REQUIRE(target == "#linuxba");
        REQUIRE(url == "http://www.baidu.com");
    }
    SECTION("a single url surrounding with 2 numbers") {
        s = ":condy!~condy@unaffiliated/condy PRIVMSG #linuxba :123 http://www.baidu.com 456";
        REQUIRE(parse_privmsg(s, from, target, url));
        REQUIRE(from == "condy");
        REQUIRE(target == "#linuxba");
        REQUIRE(url == "http://www.baidu.com");
    }
    SECTION("no url") {
        s = ":condy!~condy@unaffiliated/condy PRIVMSG #linuxba : uccu";
        REQUIRE(!parse_privmsg(s, from, target, url));
    }
}
