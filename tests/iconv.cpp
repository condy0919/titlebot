#define CATCH_CONFIG_MAIN
#include "../src/utils/iconv.hpp"
#include <catch.hpp>
#include <memory>

TEST_CASE("iconv", "[test]") {
    std::unique_ptr<iconvpp::converter> conv_ptr;
    std::unique_ptr<iconvpp::converter> conv_ptr2;
    std::string in;
    std::string out, out2;

    SECTION("euc-jp -> utf-8 for ascii") {
        conv_ptr = std::make_unique<iconvpp::converter>("euc-jp", "utf-8");
        in = "hello iconv";
        conv_ptr->convert(in, out);
        REQUIRE(in == out);
    }
    SECTION("euc-jp -> utf-8 -> euc-jp") {
        conv_ptr2 = std::make_unique<iconvpp::converter>("euc-jp", "utf-8");
        conv_ptr = std::make_unique<iconvpp::converter>("utf-8", "euc-jp");
        in = "今日は天気だ";
        conv_ptr2->convert(in, out);
        conv_ptr->convert(out, out2);
        REQUIRE(in == out2);
    }
}
