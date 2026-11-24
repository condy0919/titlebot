#include "utils.hpp"

#include <gtest/gtest.h>
#include <string>

TEST(Utils, UnquotesEntities) {
    ASSERT_EQ(tb::unquote("&amp;"), "&");
    ASSERT_EQ(tb::unquote("&lt;"), "<");
    ASSERT_EQ(tb::unquote("&mu;"), "μ");
    ASSERT_EQ(tb::unquote("template&lt;T&gt; foo"), "template<T> foo");
}

TEST(Utils, ConvertsToUtf8) {
    ASSERT_EQ(tb::utf8("fjalsdjflkds123123").value(), "fjalsdjflkds123123");

    unsigned char buf1[] =
        "\xc1\xfa\xbe\xed\xb7\xe7\xca\xd5\xd2\xf4\xbb\xfa\x20\x2d\x20\x43\x52\x61\x64\x69\x6f";
    ASSERT_EQ(tb::utf8(std::string((char*)buf1)).value(), "龙卷风收音机 - CRadio");

    unsigned char buf2[] =
        "\x20\x20\xb2\xdd\xc1\xf1\xc9\xe7\x85\x5e\x20\x20\x2d\x20\x70\x6f\x77\x65\x72\x65\x64\x20"
        "\x62\x79\x20\x70\x68\x70\x77\x69\x6e\x64\x2e\x6e\x65\x74";
    ASSERT_EQ(tb::utf8(std::string((char*)buf2)).value(), "  草榴社區  - powered by phpwind.net");
}

TEST(Utils, PreservesEmbeddedNullWhenDecodingEntities) {
    const std::string input("a\0&AMP;&#128;", 13);
    EXPECT_EQ(tb::unquote(input), std::string("a\0&€", 6));
}

TEST(Utils, DetectsTitleBeyondAsciiSample) {
    const std::string title =
        "\xa4\xb3\xa4\xec\xa4\xcf\xc6\xfc\xcb\xdc\xb8\xec\xa4\xc7\xbd\xf1\xa4\xab\xa4\xec\xa4\xbf"
        "\xa5\xda\xa1\xbc\xa5\xb8\xa4\xce\xa5\xbf\xa5\xa4\xa5\xc8\xa5\xeb\xa4\xc7\xa4\xb9\xa1\xa3";
    EXPECT_EQ(tb::utf8(title, {}, std::string(1024, ' ')),
              "これは日本語で書かれたページのタイトルです。");
}
