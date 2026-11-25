#include "decoder.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <ios>

#include "html/title_parser.hpp"

namespace {
using namespace std::string_literals;

class Decoder : public testing::Test {
protected:
    tb::TitleParser parser;
};

TEST_F(Decoder, ReadsGzipTitle) {
    const std::string path = TITLEBOT_PROJ_ROOT + "/tests/fixtures/bilibili.html.gz"s;

    tb::ContentDecoder decoder(tb::ContentEncoding::kGzip);

    std::optional<std::string> found;
    char buf[1024];
    std::ifstream ifs(path, std::ios_base::in | std::ios_base::binary);
    ASSERT_TRUE(ifs.is_open());
    while (ifs.read(buf, sizeof(buf)) || ifs.gcount()) {
        auto result =
            decoder.Consume(parser, {buf, static_cast<std::size_t>(ifs.gcount())}, ifs.eof());
        ASSERT_TRUE(result);
        if (*result) {
            found = *result;
            break;
        }
    }

    ASSERT_TRUE(found.has_value());
    ASSERT_EQ(found.value(),
              "奥巴马演唱蹲妹单曲《Call me maybe》@柚子木字幕组_哔哩哔哩_bilibili"); // NOLINT
}

TEST_F(Decoder, ContinuesAcrossGzipMembers) {
    const unsigned char compressed[] = {
        0x1f, 0x8b, 0x8,  0x0,  0x0,  0x0,  0x0,  0x0,  0x2,  0xff, 0xb3, 0x29, 0xc9,
        0x2c, 0xc9, 0x49, 0xb5, 0x2b, 0x48, 0x2c, 0x2a, 0x1,  0x0,  0x5,  0xf4, 0xfa,
        0xc,  0xb,  0x0,  0x0,  0x0,  0x1f, 0x8b, 0x8,  0x0,  0x0,  0x0,  0x0,  0x0,
        0x2,  0xff, 0x53, 0x28, 0x29, 0xcf, 0xb7, 0xd1, 0x2f, 0xc9, 0x2c, 0xc9, 0x49,
        0xb5, 0x3,  0x0,  0x18, 0x3e, 0x2d, 0x29, 0xc,  0x0,  0x0,  0x0};
    tb::ContentDecoder decoder(tb::ContentEncoding::kGzip);
    std::optional<std::string> title;
    for (unsigned char byte : compressed) {
        char ch = static_cast<char>(byte);
        auto result = decoder.Consume(parser, {&ch, 1}, false);
        ASSERT_TRUE(result);
        if (*result) {
            title = **result;
            break;
        }
    }
    EXPECT_EQ(title, "part two");
}
} // namespace
