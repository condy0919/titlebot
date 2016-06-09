#define CATCH_CONFIG_MAIN
#include "../src/utils/http.hpp"
#include <catch.hpp>
#include <string>
#include <tuple>
#include <cstring>

SCENARIO("chunk parser", "[Chunk::Parser]") {
    GIVEN("initialize") {
        Http::Chunk chunk;
        Http::Chunk::Parser parser;
        Http::Chunk::Parser::state st;
        const char* data =
            "22\r\n"
            "<html><body>it works</body></html>\r\n"
            "00\r\n"
            "\r\n";
        const char* beg = data;
        const char* end = data + std::strlen(data);

        WHEN("first chunk") {
            std::tie(st, beg) = parser.parse(chunk, beg, end);

            REQUIRE(st == Http::Chunk::Parser::good);
            REQUIRE(chunk.size_ == 34);
            REQUIRE(std::string(chunk.data_.begin(), chunk.data_.end()) ==
                    "<html><body>it works</body></html>");
            THEN("last chunk") {
                std::tie(st, beg) = parser.parse(chunk, beg, end);

                REQUIRE(st == Http::Chunk::Parser::good);
                REQUIRE(chunk.size_ == 0);
                REQUIRE(chunk.data_.empty() == true);
            }
        }
    }
}
