#define CATCH_CONFIG_MAIN
#include "../src/dns_cache.hpp"
#include <catch.hpp>
#include <thread>
#include <chrono>


SCENARIO("DNS Cache", "[DNS Cache]") {
    GIVEN("init") {
        DNSCache<1> cache;
        std::string service, host;

        WHEN("put pair {http, www.baidu.com}") {
            service = "http";
            host = "www.baidu.com";
            
            cache.put(service, host, boost::asio::ip::tcp::resolver::iterator());

            THEN("get immediately") {
                auto opt = cache.get(service, host);
                REQUIRE(bool(opt));
            }
        }

        WHEN("put pair {http, www.baidu.com}") {
            service = "http";
            host = "www.baidu.com";

            cache.put(service, host, boost::asio::ip::tcp::resolver::iterator());
            THEN("sleep 1s") {
                std::this_thread::sleep_for(std::chrono::seconds(1));
                auto opt = cache.get(service, host);
                REQUIRE(!bool(opt));
            }
        }

        WHEN("put pair {http, www.baidu.com}") {
            service = "http";
            host = "www.baidu.com";

            cache.put(service, host, boost::asio::ip::tcp::resolver::iterator());
            REQUIRE(cache.size() == 1);
            THEN("remove it") {
                cache.erase(service, host);
                REQUIRE(cache.empty());
            }
        }
    }
}
