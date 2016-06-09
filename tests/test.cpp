#include "../fetcher.hpp"
#include <iostream>

int main() {
    Fetcher f;
    f.start("wkar.org", "/programs/record",
            [](std::string s) { std::cout << "recv " << s << std::endl; });
    f.run();

    return 0;
}
