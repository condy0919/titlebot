#include "../src/fetcher.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    assert(argc == 4);
    std::string schema = argv[1], host = argv[2], uri = argv[3];

    Fetcher f;
    f.start(schema, host, uri,
            [](std::string s) { std::cout << s << std::endl; });
    f.run();

    return 0;
}
