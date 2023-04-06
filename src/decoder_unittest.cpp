#include "decoder.hpp"
#include "title_parser.hpp"

#include <fstream>

#include <doctest/doctest.h>
#include <ios>
#include <memory>

using namespace std::string_literals;

TEST_CASE("GzipDecoder") {
    const std::string path = TITLEBOT_PROJ_ROOT + "/assets/bilibili.html.gz"s;

    tb::TitleParser tparser;
    std::unique_ptr<tb::ContentDecoder> decoder = std::make_unique<tb::GzipDecoder>();

    std::optional<std::string> found;
    char buf[1024];
    std::ifstream ifs(path, std::ios_base::in | std::ios_base::binary);
    while (std::streamsize sz = ifs.readsome(buf, sizeof(buf))) {
        if (auto result = decoder->parse(tparser, buf, buf + sz)) {
            found = result;
            break;
        }
    }

    REQUIRE(found.has_value());
    REQUIRE_EQ(found.value(), "奥巴马演唱蹲妹单曲《Call me maybe》@柚子木字幕组_哔哩哔哩_bilibili"); // NOLINT
}
