#include "mobot.hpp"
#include "fetcher.hpp"

class Server {
public:
    Server();

    void start();

private:
    MoBot bot_;
    Fetcher fetcher_;
};
