#include "irc/mobot.hpp"
#include "http/fetcher.hpp"

class Server {
public:
    Server();

    void start();

private:
    MoBot bot_;
    Fetcher fetcher_;
};
