#include <boost/asio.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <iostream>
#include <utility>
#include <vector>
#include <string>
#include <random>
#include <regex>

class IRCBot {
public:
    IRCBot(std::string server, int port)
        : IRCBot(std::move(server), std::to_string(port)) {}

    IRCBot(std::string server, std::string port)
        : server_(std::move(server)),
          port_(std::move(port)),
          beat_(service_, boost::posix_time::minutes(9)),
          sock_(service_) {
        connect();
    }

    ~IRCBot() {
        disconnect();
    }

    void start() {
        beat_.async_wait(&IRCBot::timeout);
        service_.run();
    }

protected:
    void async_write(const char* buf, size_t len) {
        boost::asio::async_write(
            sock_, boost::asio::buffer(buf, len),
            [=](boost::system::error_code ec, size_t sent) {
                if (ec) {
                    std::cerr << "[error] "
                              << boost::format("sent %d/%d") % sent % len
                              << '\n';
                    throw "[error] exit";
                }
            });
    }

    void async_write(const char* buf) {
        async_write(buf, std::strlen(buf));
    }

    void async_write(std::string s) {
        async_write(s.data(), s.length());
    }

    void async_read(std::function<void(std::string)> callback) {
        boost::asio::async_read_until(
            sock_, buf_, "\r\n",
            [this, callback](boost::system::error_code ec, size_t sent __attribute__((unused))) {
                if (!ec) {
                    std::istream is(&buf_);
                    std::string line;

                    while (std::getline(is, line)) {
                        if (line.compare(0, 4, "PING") == 0) {
                            async_write("PONG" + line.substr(4) + "\r\n");
                            beat_.expires_from_now(
                                boost::posix_time::minutes(9));
                            beat_.async_wait(&IRCBot::timeout);
                        } else {
                            if (line.back() == '\r') {
                                line.pop_back();
                            }
                            callback(line);
                        }
                        std::cout << "[info] " << line << '\n';
                    }
                } else {
                    boost::asio::streambuf::const_buffers_type bufs = buf_.data();
                    std::string s(boost::asio::buffers_begin(bufs),
                                  boost::asio::buffers_end(bufs));
                    std::cerr << "[error] "
                              << "read " << s << '\n';
                    throw "[error] exit";
                }
                async_read(callback);
            });
    }

private:
    static void timeout(const boost::system::error_code& ec) {
        if (ec != boost::asio::error::operation_aborted) {
            throw "[timeout] goodbye";
        }
    }

    void connect() {
        using namespace boost::asio;
        ip::tcp::resolver resolver(service_);
        ip::tcp::resolver::query query(server_, port_);
        ip::tcp::resolver::iterator iter = resolver.resolve(query);
        ip::tcp::endpoint ep = *iter;

        std::cout << "[info] connect to " << ep.address().to_string() << ":"
                  << ep.port() << '\n';

        sock_.open(ip::tcp::v4());
        sock_.connect(ep);
    }

    void disconnect() {
        sock_.close();
    }

    std::string server_, port_;
    boost::asio::io_service service_;
    boost::asio::deadline_timer beat_;
    boost::asio::ip::tcp::socket sock_;
    boost::asio::streambuf buf_;
};

class MoBot : public IRCBot {
public:
    MoBot() : IRCBot("irc.freenode.net", 6667) {}

    /// NICK [nickname]
    MoBot& nick(std::string nick) {
        return raw_send("NICK " + nick);
    }

    /// user [mode] [unused] [realname]
    MoBot& user(std::string user) {
        return raw_send("USER " + user + " 0 * :MoBot");
    }

    /// privmsg [target] [msg]
    MoBot& privmsg(std::string msg, std::string to) {
        return raw_send("PRIVMSG " + to + " :" + msg);
    }

    /// join [#channel]
    MoBot& join(std::vector<std::string> channels) {
        for (auto&& chan : channels) {
            raw_send("JOIN " + chan);
        }
        return *this;
    }

    void mainloop(std::function<void(std::string)> callback) {
        auto fn = [=](std::string s) {
            std::regex pattern(".* PRIVMSG (\\S+) :.*(http://\\S+)");
            std::smatch match;
            if (std::regex_match(s, match, pattern)) {
                if (match.ready()) {
                    std::string target = match[1].str();
                    std::string url = match[2].str();
                    callback(std::move(url));
                }
            }
        };
        async_read(fn);
        start();
    }

private:
    MoBot& raw_send(std::string s) {
        async_write(s + "\r\n");
        return *this;
    }
};
