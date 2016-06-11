#include "../src/fetcher.hpp"
#include <iostream>

int main() {
    Fetcher f;
    f.start("https", "www.baidu.com", "/",
            [](std::string s) { std::cout << "recv " << s << std::endl; });
    f.run();

    return 0;
}
