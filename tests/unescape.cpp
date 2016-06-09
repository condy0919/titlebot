#define CATCH_CONFIG_MAIN
#include "../src/utils/entities.hpp"
#include <catch.hpp>
#include <string>
#include <iostream>

TEST_CASE("HTMLEntitiesDecoder", "[HTMLEntitiesDecoder]") {
    std::string str;

    SECTION("&amp;") {
        str = Html::Unescape("&amp;");
        REQUIRE(str == "&");
    }
    SECTION("&lt;") {
        str = Html::Unescape("&lt;");
        REQUIRE(str == "<");
    }
    SECTION("&mu;") {
        str = Html::Unescape("&mu;");
        REQUIRE(str == "μ");
    }
}
