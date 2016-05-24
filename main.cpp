#include "fetcher.hpp"

int main(int argc, char* argv[]) {
    std::string url = "http://www.acgdoge.net/archives/8875";
    std::string host = "www.acgdoge.net", uri = "/archives/8875";

    Fetcher fetcher;
    fetcher.start(host, uri);

    fetcher.run();

    return 0;
}
