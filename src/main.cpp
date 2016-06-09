#include "server.hpp"

int main() {
    Server serv;
    serv.start();

    //Fetcher fetcher;
    //fetcher.start("www.acgdoge.net", "/archives/8875", [](std::string s) {
    //    std::cout << "recv " << s << std::endl;
    //});
    //fetcher.run();

    return 0;
}
