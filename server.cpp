#include "server.hpp"
#include "utils/log.hpp"
#include <vector>
#include <thread>

Server::Server() = default;

void Server::start() {
    std::function<void(std::string)> send_callback(
        std::bind(&MoBot::privmsg, &bot_, std::placeholders::_1, "#linuxba"));
    std::function<void(std::string, std::string,
                       std::function<void(std::string)>)>
        callback(std::bind(&Fetcher::start, &fetcher_, std::placeholders::_1,
                           std::placeholders::_2, std::placeholders::_3));

    std::thread fetcher_thread(&Fetcher::run, &fetcher_);
    std::thread mobot_thread([&]() {
        bot_.nick("BruceSucksBot").user("BruceSucksBot").join({"#linuxba"});
        try {
            bot_.mainloop([=](std::string url) {
                std::string tmp = url.substr(7);
                std::string host = tmp, uri = "/";
                auto iter = std::find(tmp.begin(), tmp.end(), '/');
                if (iter != tmp.end()) {
                    host = std::string(tmp.begin(), iter);
                    uri = std::string(iter, tmp.end());
                }
                callback(host, uri, send_callback);
            });
        } catch (const char* err) {
            std::cerr << err << '\n';
            std::exit(-1);
        }
    });

    if (mobot_thread.joinable()) {
        mobot_thread.join();
    }
    if (fetcher_thread.joinable()) {
        fetcher_thread.join();
    }
}
