#include "../fetcher.hpp"
#include <iostream>

int main() {
    Fetcher f;
    f.start("music.163.com", "/#/song?id=407677659",
            [](std::string s) { std::cout << "recv " << s << std::endl; });
    f.run();

    return 0;
}
