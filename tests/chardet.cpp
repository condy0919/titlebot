#define CATCH_CONFIG_MAIN
#include "../src/utils/iconv.hpp"
#include "../src/utils/chardet.hpp"
#include <strings.h>
#include <catch.hpp>
#include <cstring>
#include <iostream>

TEST_CASE("Char Detector", "[Char Detector]") {
    CharDetector det;
    std::string s;
    const char* charset = nullptr;

    SECTION("test ascii") {
        det.reset();
        s = "dsfjfowifjewfjflkdsjflafj\'\'123123";

        REQUIRE(det.feed(s.data(), s.size()));
        det.term();
        charset = det.getCharset();
        REQUIRE((std::strlen(charset) == 0 ||
                 strncasecmp(charset, "ASCII", sizeof("ASCII") - 1) == 0));
    }
    SECTION("test utf-8") {
        det.reset();
        s = "UTF-8中文测试";

        REQUIRE(det.feed(s.data(), s.size()));
        det.term();
        charset = det.getCharset();
        REQUIRE(std::strcmp(charset, "UTF-8") == 0);
    }
    SECTION("test gb2312") {
        det.reset();
        s = "中文测试，这里是中文";

        std::string out;
        iconvpp::converter conv("GB2312", "UTF-8");
        conv.convert(s, out);
        REQUIRE(det.feed(out.data(), out.size()));
        det.term();
        charset = det.getCharset();
        
        std::string out2;
        iconvpp::converter reconv("UTF-8", charset);
        reconv.convert(out, out2);
        REQUIRE(out2 == s);
    }
}
