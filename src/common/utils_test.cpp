#include "utils.hpp"

#include <cstring>
#include <tuple>

#include <gtest/gtest.h>

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

TEST(Utils, ParsesHttpChunks) {
    const char* data = "22\r\n"
                       "<html><body>it works</body></html>\r\n"
                       "00\r\n"
                       "\r\n";

    tb::HttpChunk chunk;
    tb::HttpChunk::Parser parser;
    tb::HttpChunk::Parser::Status status;

    const char* begin = data;
    const char* end = data + std::strlen(data);
    std::tie(status, begin) = parser.parse(chunk, begin, end);
    ASSERT_EQ(status, tb::HttpChunk::Parser::Status::Good);
    ASSERT_EQ(chunk.size(), 34);
    ASSERT_EQ(chunk.data().size(), 34);
    ASSERT_EQ(std::string(chunk.data().begin(), chunk.data().end()),
               "<html><body>it works</body></html>");

    std::tie(status, begin) = parser.parse(chunk, begin, end);
    ASSERT_EQ(status, tb::HttpChunk::Parser::Status::Good);
    ASSERT_EQ(chunk.size(), 0);
    ASSERT_TRUE(chunk.data().empty());
}
