#include "../decoder.hpp"
#include <iostream>
#include <fstream>
#include <memory>

int main() {
    std::unique_ptr<ContentDecoder> decoder = std::make_unique<GzipDecoder>();
    std::ifstream ifs("bili.html.gz", std::ios_base::in | std::ios_base::binary);
    char line[1024];

    while (true) {
        int sz = ifs.readsome(line, sizeof(line));
        auto out = decoder->decode(line, sz);
        std::cout << out;
        if (sz == 0)
            break;
    }

    return 0;
}
