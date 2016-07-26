#define CATCH_CONFIG_MAIN
#include "../src/mobot.hpp"
#include <catch.hpp>
#include <iostream>
#include <string>
#include <tuple>


TEST_CASE("PRIVMSG Parser", "[PRIVMSG Parser]") {
    std::string s;
    std::string from, target, protocol, url;
    SECTION("a single url") {
        s = ":condy!~condy@unaffiliated/condy PRIVMSG #linuxba :http://www.baidu.com";
        REQUIRE(parse_privmsg(s, from, target, protocol, url));
        REQUIRE(from == "condy");
        REQUIRE(target == "#linuxba");
        REQUIRE(protocol == "http");
        REQUIRE(url == "www.baidu.com");
    }
    SECTION("a single url surrounding with 2 numbers") {
        s = ":condy!~condy@unaffiliated/condy PRIVMSG #linuxba :123 https://www.baidu.com 456";
        REQUIRE(parse_privmsg(s, from, target, protocol, url));
        REQUIRE(from == "condy");
        REQUIRE(target == "#linuxba");
        REQUIRE(protocol == "https");
        REQUIRE(url == "www.baidu.com");
    }
    SECTION("no url") {
        s = ":condy!~condy@unaffiliated/condy PRIVMSG #linuxba : uccu";
        REQUIRE(!parse_privmsg(s, from, target, protocol, url));
    }
    SECTION("chinese symbol") {
        s = ":condy!~condy@unaffiliated/condy PRIVMSG #linuxba :https://www.baidu.com（";
        REQUIRE(parse_privmsg(s, from, target, protocol, url));
        REQUIRE(from == "condy");
        REQUIRE(target == "#linuxba");
        REQUIRE(protocol == "https");
        REQUIRE(url == "www.baidu.com");
    }
    SECTION(":condy!~condy@unaffiliated/condy PRIVMSG #linuxba : https://\"http://www.baidu.com") {
        s = ":condy!~condy@unaffiliated/condy PRIVMSG #linuxba : https://\"http://www.baidu.com";
        REQUIRE(parse_privmsg(s, from, target, protocol, url));
        REQUIRE(from == "condy");
        REQUIRE(target == "#linuxba");
        REQUIRE(protocol == "http");
        REQUIRE(url == "www.baidu.com");
    }
    SECTION("colorful privmsg message") {
        // \x0331
        s = ":condy!~condy@unaffiliated/condy PRIVMSG #linuxba :\x03\x30\x31http://www.baidu.com\x03";
        REQUIRE(parse_privmsg(s, from, target, protocol, url));
        REQUIRE(from == "condy");
        REQUIRE(target == "#linuxba");
        REQUIRE(protocol == "http");
        REQUIRE(url == "www.baidu.com");

        // \x033,12
        s = ":condy!~condy@unaffiliated/condy PRIVMSG #linuxba :\x03\x30,12http://www.baidu.com";
        REQUIRE(parse_privmsg(s, from, target, protocol, url));
        REQUIRE(from == "condy");
        REQUIRE(target == "#linuxba");
        REQUIRE(protocol == "http");
        REQUIRE(url == "www.baidu.com");
    }
    SECTION("url starting with digits") {
        s = ":condy!~condy@unaffiliated/condy PRIVMSG #linuxba :>   http://2016.sina.cn/zq/2016-07-24/detail-ifxuhukz0921444.d.html?wm=3049_0015 测试";
        REQUIRE(parse_privmsg(s, from, target, protocol, url));
        REQUIRE(from == "condy");
        REQUIRE(target == "#linuxba");
        REQUIRE(protocol == "http");
        REQUIRE(url == "2016.sina.cn/zq/2016-07-24/detail-ifxuhukz0921444.d.html?wm=3049_0015");
    }
}
