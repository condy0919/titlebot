#include "../fetcher.hpp"
#include <iostream>

int main() {
    Fetcher f;
    f.start("www.paterva.com", "/redirect/m3ceregister.html",
            [](std::string s) { std::cout << "recv " << s << std::endl; });
    f.run();

    return 0;
}
