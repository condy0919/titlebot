#include "server.hpp"
#include "setting.hpp"
#include "utils/log.hpp"
#include <vector>
#include <thread>

Server::Server() = default;

void Server::start() {
    std::thread fetcher_thread(&Fetcher::run, &fetcher_);
    std::thread mobot_thread([&]() {
        bot_.nick(TITLEBOT_NICK).user(TITLEBOT_USER).join(TITLEBOT_CHANNELS);
        try {
            // capture this pointer!
            bot_.mainloop([=](std::string protocol, std::string url,
                              std::string target) {
                std::string host, uri;
                std::tie(std::ignore, host, uri) = Http::parseURL(url);
                fetcher_.start(
                    std::move(protocol), std::move(host), std::move(uri),
                    std::bind(&MoBot::privmsg, &bot_, std::placeholders::_1,
                              std::move(target)));
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
