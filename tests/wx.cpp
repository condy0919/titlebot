#include "../fetcher.hpp"
#include <iostream>

int main() {
    Fetcher f;
    f.start("http", "mp.weixin.qq.com", "/s?__biz=MzIwNjEwMzkzNw==&mid=2652061164&idx=1&sn=83ecd46f9125efce135ba2f79b3ff65d&scene=2&srcid=0525uYF4sJqlSHrBTkiAtgtE&from=timeline&isappinstalled=0",
            [](std::string s) { std::cout << "recv " << s << std::endl; });
    f.run();

    return 0;
}
