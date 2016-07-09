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
        unsigned char buf[]="\xc1\xfa\xbe\xed\xb7\xe7\xca\xd5\xd2\xf4\xbb\xfa\x20\x2d\x20\x43\x52\x61\x64\x69\x6f";
        out = tran.trans(std::string((char*)buf));
        std::cout << out << '\n';
        REQUIRE(out == "龙卷风收音机 - CRadio");
    }
    SECTION("caoliu") {
        UTF8Translator tran;
        unsigned char buf[] = "\x20\x20\xb2\xdd\xc1\xf1\xc9\xe7\x85\x5e\x20\x20\x2d\x20\x70\x6f\x77\x65\x72\x65\x64\x20\x62\x79\x20\x70\x68\x70\x77\x69\x6e\x64\x2e\x6e\x65\x74";
        out = tran.trans(std::string((char*)buf));
        std::cout << out << '\n';
        REQUIRE(out == "  草榴社區  - powered by phpwind.net");
    }
}
