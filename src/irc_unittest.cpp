#include "irc.hpp"

#include "doctest/doctest.h"

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
