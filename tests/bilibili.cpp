#include "../fetcher.hpp"
#include <iostream>

int main() {
    Fetcher f;
    // bilibili will always return gzipped data
    // test failed
    f.start("www.bilibili.com", "/video/av4761887/",
            [](std::string s) { std::cout << "recv " << s << std::endl; });
    f.run();

    return 0;
}
