#define CATCH_CONFIG_MAIN
#include "../src/utils/iconv.hpp"
#include "../src/utils/utf8tran.hpp"
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
    SECTION("龙卷风收音机 - CRadio") {
        UTF8Translator tran;
        unsigned char buf[] = {
            0xc1, 0xfa, 0xbe, 0xed, 0xb7, 0xe7, 0xca, 0xd5, 0xd2, 0xf4, 0xbb,
            0xfa, 0x20, 0x2d, 0x20, 0x43, 0x52, 0x61, 0x64, 0x69, 0x6f, '\0'
        };
        out = tran.trans(std::string((char*)buf));
        std::cout << out << '\n';
        REQUIRE(out == "龙卷风收音机 - CRadio");
    }
    SECTION("caoliu") {
        UTF8Translator tran;
        unsigned char buf[] = {
            0x20, 0x20, 0xb2, 0xdd, 0xc1, 0xf1, 0xc9, 0xe7, 0x85, 0x5e, 0x20,
            0x20, 0x2d, 0x20, 0x70, 0x6f, 0x77, 0x65, 0x72, 0x65, 0x64, 0x20,
            0x62, 0x79, 0x20, 0x70, 0x68, 0x70, 0x77, 0x69, 0x6e, 0x64, 0x2e,
            0x6e, 0x65, 0x74
        };
        out = tran.trans(std::string((char*)buf));
        std::cout << out << '\n';
        REQUIRE(out == "  草榴社區  - powered by phpwind.net");
    }
}
