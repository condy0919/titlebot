#include "irc.hpp"

#include <doctest/doctest.h>

TEST_CASE("TextFormat") {
    // \x3 0 1 hello => hello
    REQUIRE_EQ(tb::normalize("\x3\x30\x31hello"), "hello");
    // \x3 0 1 , => ,
    REQUIRE_EQ(tb::normalize("\x3\x30\x31,"), ",");
    // \x3 0 1 , irc => , irc
    REQUIRE_EQ(tb::normalize("\x3\x30\x31,irc"), ",irc");
    // \x3 0 1 , 2 0 =>
    REQUIRE_EQ(tb::normalize("\x3\x30\x31,20"), "");
    // \x3 0 1 , 2
    REQUIRE_EQ(tb::normalize("\x3\x30\x31,2"), "");
    // \x3 9 9 , 9 8 world \x3 => world
    REQUIRE_EQ(tb::normalize("\x3\x39\x39,98world\x3"), "world");
}

TEST_CASE("ParsePrivmsg") {
    auto msg = tb::parse_privmsg(
        ":condy!~condy@unaffiliated/condy PRIVMSG #linuxba :http://www.baidu.com");
    REQUIRE(msg.has_value());
    REQUIRE_EQ(msg.value().who, "condy");
    REQUIRE_EQ(msg.value().channel, "#linuxba");
    REQUIRE_EQ(msg.value().protocol, "http");
    REQUIRE_EQ(msg.value().url, "www.baidu.com");

    msg = tb::parse_privmsg(
        ":condy!~condy@unaffiliated/condy PRIVMSG #linuxba :123 http://www.baidu.com 456");
    REQUIRE(msg.has_value());
    REQUIRE_EQ(msg.value().who, "condy");
    REQUIRE_EQ(msg.value().channel, "#linuxba");
    REQUIRE_EQ(msg.value().protocol, "http");
    REQUIRE_EQ(msg.value().url, "www.baidu.com");

    msg = tb::parse_privmsg(
        ":condy!~condy@unaffiliated/condy PRIVMSG #linuxba :123456789 uccu, xmsl");
    REQUIRE_FALSE(msg.has_value());

    msg = tb::parse_privmsg(
        ":condy!~condy@unaffiliated/condy PRIVMSG #linuxba :https://www.baidu.com（");
    REQUIRE(msg.has_value());
    REQUIRE_EQ(msg.value().who, "condy");
    REQUIRE_EQ(msg.value().channel, "#linuxba");
    REQUIRE_EQ(msg.value().protocol, "https");
    REQUIRE_EQ(msg.value().url, "www.baidu.com");

    msg = tb::parse_privmsg(
        ":condy!~condy@unaffiliated/condy PRIVMSG #linuxba :https://\"http://www.baidu.com");
    REQUIRE(msg.has_value());
    REQUIRE_EQ(msg.value().who, "condy");
    REQUIRE_EQ(msg.value().channel, "#linuxba");
    REQUIRE_EQ(msg.value().protocol, "http");
    REQUIRE_EQ(msg.value().url, "www.baidu.com");

    // colorful privmsg \x03 '3' '1'
    msg = tb::parse_privmsg(":condy!~condy@unaffiliated/condy PRIVMSG #linuxba "
                            ":\x03\x30\x31https://\"http://www.baidu.com\x03");
    REQUIRE(msg.has_value());
    REQUIRE_EQ(msg.value().who, "condy");
    REQUIRE_EQ(msg.value().channel, "#linuxba");
    REQUIRE_EQ(msg.value().protocol, "http");
    REQUIRE_EQ(msg.value().url, "www.baidu.com");

    // colorful privmsg \x03 '3', '1' '2'
    msg = tb::parse_privmsg(":condy!~condy@unaffiliated/condy PRIVMSG #linuxba "
                            ":\x03\x30,12https://\"http://www.baidu.com");
    REQUIRE(msg.has_value());
    REQUIRE_EQ(msg.value().who, "condy");
    REQUIRE_EQ(msg.value().channel, "#linuxba");
    REQUIRE_EQ(msg.value().protocol, "http");
    REQUIRE_EQ(msg.value().url, "www.baidu.com");

    // url starting with digits
    msg = tb::parse_privmsg(
        ":condy!~condy@unaffiliated/condy PRIVMSG #linuxba :>   "
        "http://2016.sina.cn/zq/abc.html 测试");
    REQUIRE(msg.has_value());
    REQUIRE_EQ(msg.value().who, "condy");
    REQUIRE_EQ(msg.value().channel, "#linuxba");
    REQUIRE_EQ(msg.value().protocol, "http");
    REQUIRE_EQ(msg.value().url, "2016.sina.cn/zq/abc.html");
}
