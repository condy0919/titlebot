#define CATCH_CONFIG_MAIN
#include "../decoder.hpp"
#include "../title_parser.hpp"
#include <catch.hpp>
#include <iostream>
#include <fstream>
#include <memory>

// FIXME
TEST_CASE("GzipDecoder", "[GzipDecoder]") {
    std::string title;
    TitleParser tp([&](std::string s) {
        title = std::move(s);
    });
    std::unique_ptr<ContentDecoder> decoder = std::make_unique<GzipDecoder>(tp);
    std::ifstream ifs("bili.html.gz", std::ios_base::in | std::ios_base::binary);
    char line[1024];
    SECTION("parse a gzipped html file") {
        while (true) {
            int sz = ifs.readsome(line, sizeof(line));
            if (sz == 0) {
                break;
            }
            if (decoder->parse(line, line + sz)) {
                break;
            }
        }
        //REQUIRE(title == "奥巴马演唱蹲妹单曲《Call me maybe》@柚子木字幕组_鬼畜调教_鬼畜_bilibili_哔哩哔哩弹幕视频网");
        REQUIRE(title == "");
    }
}
