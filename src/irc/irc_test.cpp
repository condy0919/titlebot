#include "irc.hpp"

#include <gtest/gtest.h>

TEST(Irc, NormalizesFormatting) {
    // \x3 0 1 hello => hello
    ASSERT_EQ(tb::normalize("\x3\x30\x31hello"), "hello");
    // \x3 0 1 , => ,
    ASSERT_EQ(tb::normalize("\x3\x30\x31,"), ",");
    // \x3 0 1 , irc => , irc
    ASSERT_EQ(tb::normalize("\x3\x30\x31,irc"), ",irc");
    // \x3 0 1 , 2 0 =>
    ASSERT_EQ(tb::normalize("\x3\x30\x31,20"), "");
    // \x3 0 1 , 2
    ASSERT_EQ(tb::normalize("\x3\x30\x31,2"), "");
    // \x3 9 9 , 9 8 world \x3 => world
    ASSERT_EQ(tb::normalize("\x3\x39\x39,98world\x3"), "world");
}

TEST(Irc, ParsesPrivmsg) {
    auto msg = tb::parse_privmsg(
        ":condy!~condy@unaffiliated/condy PRIVMSG #linuxba :http://www.baidu.com");
    ASSERT_TRUE(msg.has_value());
    ASSERT_EQ(msg.value().who, "condy");
    ASSERT_EQ(msg.value().channel, "#linuxba");
    ASSERT_EQ(msg.value().protocol, "http");
    ASSERT_EQ(msg.value().url, "www.baidu.com");

    msg = tb::parse_privmsg(
        ":condy!~condy@unaffiliated/condy PRIVMSG #linuxba :123 http://www.baidu.com 456");
    ASSERT_TRUE(msg.has_value());
    ASSERT_EQ(msg.value().who, "condy");
    ASSERT_EQ(msg.value().channel, "#linuxba");
    ASSERT_EQ(msg.value().protocol, "http");
    ASSERT_EQ(msg.value().url, "www.baidu.com");

    msg = tb::parse_privmsg(
        ":condy!~condy@unaffiliated/condy PRIVMSG #linuxba :123456789 uccu, xmsl");
    ASSERT_FALSE(msg.has_value());

    msg = tb::parse_privmsg(
        ":condy!~condy@unaffiliated/condy PRIVMSG #linuxba :https://www.baidu.com（");
    ASSERT_TRUE(msg.has_value());
    ASSERT_EQ(msg.value().who, "condy");
    ASSERT_EQ(msg.value().channel, "#linuxba");
    ASSERT_EQ(msg.value().protocol, "https");
    ASSERT_EQ(msg.value().url, "www.baidu.com");

    msg = tb::parse_privmsg(
        ":condy!~condy@unaffiliated/condy PRIVMSG #linuxba :https://\"http://www.baidu.com");
    ASSERT_TRUE(msg.has_value());
    ASSERT_EQ(msg.value().who, "condy");
    ASSERT_EQ(msg.value().channel, "#linuxba");
    ASSERT_EQ(msg.value().protocol, "http");
    ASSERT_EQ(msg.value().url, "www.baidu.com");

    // colorful privmsg \x03 '3' '1'
    msg = tb::parse_privmsg(":condy!~condy@unaffiliated/condy PRIVMSG #linuxba "
                            ":\x03\x30\x31https://\"http://www.baidu.com\x03");
    ASSERT_TRUE(msg.has_value());
    ASSERT_EQ(msg.value().who, "condy");
    ASSERT_EQ(msg.value().channel, "#linuxba");
    ASSERT_EQ(msg.value().protocol, "http");
    ASSERT_EQ(msg.value().url, "www.baidu.com");

    // colorful privmsg \x03 '3', '1' '2'
    msg = tb::parse_privmsg(":condy!~condy@unaffiliated/condy PRIVMSG #linuxba "
                            ":\x03\x30,12https://\"http://www.baidu.com");
    ASSERT_TRUE(msg.has_value());
    ASSERT_EQ(msg.value().who, "condy");
    ASSERT_EQ(msg.value().channel, "#linuxba");
    ASSERT_EQ(msg.value().protocol, "http");
    ASSERT_EQ(msg.value().url, "www.baidu.com");

    // url starting with digits
    msg = tb::parse_privmsg(
        ":condy!~condy@unaffiliated/condy PRIVMSG #linuxba :>   "
        "http://2016.sina.cn/zq/abc.html 测试");
    ASSERT_TRUE(msg.has_value());
    ASSERT_EQ(msg.value().who, "condy");
    ASSERT_EQ(msg.value().channel, "#linuxba");
    ASSERT_EQ(msg.value().protocol, "http");
    ASSERT_EQ(msg.value().url, "2016.sina.cn/zq/abc.html");
}

TEST(Irc, PreservesUnicodeMessageBoundaries) {
    EXPECT_EQ(tb::normalize("测试"), "测试");
    auto msg = tb::parse_privmsg(":a!b PRIVMSG #r :https://example.org（测试v2）");
    ASSERT_TRUE(msg);
    EXPECT_EQ(msg->url, "example.org");
}

TEST(Irc, BoundsColorParameters) {
    EXPECT_EQ(tb::normalize("\x03" "04,2https://2016.example"), "https://2016.example");
    EXPECT_EQ(tb::normalize("\x03" "4\x03" "12text"), "text");
}
