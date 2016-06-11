#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/format.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/algorithm/string.hpp>
#include <iostream>
#include <utility>
#include <chrono>
#include <vector>
#include <string>
#include <random>


class IRCBot {
public:
    IRCBot(std::string server, int port)
        : IRCBot(std::move(server), std::to_string(port)) {}

    IRCBot(std::string server, std::string port)
        : server_(std::move(server)),
          port_(std::move(port)),
          read_timer_(service_),
          sock_(service_) {
        connect();
    }

    ~IRCBot() {
        disconnect();
    }

    void start() {
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

    void async_write(const std::string& s) {
        async_write(s.data(), s.length());
    }

    void async_read(std::function<void(std::string)> callback) {
        read_timer_.expires_from_now(std::chrono::minutes(5));
        read_timer_.async_wait(boost::bind(&IRCBot::timeout, this,
                                           boost::asio::placeholders::error));
        boost::asio::async_read_until(
            sock_, buf_, "\r\n",
            [this, callback](boost::system::error_code ec, size_t sz __attribute__((unused))) {
                if (!ec) {
                    read_timer_.cancel_one(ec);

                    std::istream is(&buf_);
                    std::string line;

                    std::getline(is, line);
                    if (line.compare(0, 4, "PING") == 0) {
                        async_write("PONG" + line.substr(4) + "\n");
                    } else {
                        line.pop_back();
                        callback(line);
                    }
                    std::cout << "[info] " << line << '\n';
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
    void timeout(const boost::system::error_code& ec) {
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
        sock_.connect(ep);
    }

    void disconnect() {
        sock_.close();
    }

    std::string server_, port_;
    boost::asio::io_service service_;
    boost::asio::steady_timer read_timer_;
    boost::asio::ip::tcp::socket sock_;
    boost::asio::streambuf buf_;
};

namespace {
bool is_tspecial(int c) {
    switch (c) {
    case '(':
    case ')':
    case '<':
    case '>':
    case '[':
    case ']':
    case '{':
    case '}':
    case '^':
    case '|':
    // case ':': // XXX
    case '\\':
    case '\"':
    case ' ':
        return true;
    default:
        return false;
    }
    return false;
}

bool parse_privmsg(std::string s, std::string& from, std::string& target,
                   std::string& protocol, std::string& url) {
    if (s.front() != ':') {
        return false;
    }

    auto from_end = std::find(s.begin() + 1, s.end(), '!');
    if (from_end == s.end()) {
        return false;
    }
    from = std::string(s.begin() + 1, from_end); // Out

    auto privmsg_start = std::find(from_end, s.end(), ' ');
    if (privmsg_start == s.end()) {
        return false;
    }

    const std::uint64_t magic_privmsg = 0x47534d5649525020;  // " PRIVMSG"
    if (magic_privmsg != *(std::uint64_t*)&*privmsg_start) {
        return false;
    }

    auto iter = privmsg_start + 9;
    auto target_end = std::find(iter, s.end(), ' ');
    if (target_end == s.end()) {
        return false;
    }
    target = std::string(iter, target_end); // Out

    iter = target_end + 2;
    std::string tmp;
    enum {
        TOKEN_OTHER,
        TOKEN_H,
        TOKEN_T_1,
        TOKEN_T_2,
        TOKEN_P,
        TOKEN_S,
        TOKEN_COLON,
        TOKEN_SLASH_1,
        TOKEN_SLASH_2,
        TOKEN_CONTENT
    } st = TOKEN_OTHER;
    bool over = false;
    bool https = false;
    for (auto i = iter; !over && i != s.end(); ++i) {
        const char c = *i;
        switch (st) {
        case TOKEN_OTHER:
            https = false;
            if (c == 'h' || c == 'H') {
                st = TOKEN_H;
            }
            break;

        case TOKEN_H:
            if (c == 't' || c == 'T') {
                st = TOKEN_T_1;
            } else {
                st = TOKEN_OTHER;
            }
            break;

        case TOKEN_T_1:
            if (c == 't' || c == 'T') {
                st = TOKEN_T_2;
            } else {
                st = TOKEN_OTHER;
            }
            break;

        case TOKEN_T_2:
            if (c == 'p' || c == 'P') {
                st = TOKEN_P;
            } else {
                st = TOKEN_OTHER;
            }
            break;

        case TOKEN_P:
            if (c == 's' || c == 'S') {
                https = true;
                st = TOKEN_S;
            } else if (c == ':') {
                st = TOKEN_COLON;
            } else {
                st = TOKEN_OTHER;
            }
            break;

        case TOKEN_S:
            if (c == ':') {
                st = TOKEN_COLON;
            } else {
                st = TOKEN_OTHER;
            }
            break;

        case TOKEN_COLON:
            if (c == '/') {
                st = TOKEN_SLASH_1;
            } else {
                st = TOKEN_OTHER;
            }
            break;

        case TOKEN_SLASH_1:
            if (c == '/') {
                st = TOKEN_SLASH_2;
            } else {
                st = TOKEN_OTHER;
            }
            break;

        case TOKEN_SLASH_2:
            if (!std::isspace(c)) {
                tmp.push_back(c);
                st = TOKEN_CONTENT;
            } else {
                st = TOKEN_OTHER;
            }
            break;

        case TOKEN_CONTENT:
            if (!is_tspecial(c)) {
                tmp.push_back(c);
            } else {
                over = true;
            }
            break;
        }
    }
    if (!tmp.empty()) {
        url = std::move(tmp);
        protocol = https ? "https" : "http";
        return true;
    }
    return false;
}
}

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

    MoBot& join(std::string channels) {
        std::istringstream iss(std::move(channels));
        std::vector<std::string> chs;
        std::string ch;

        while (std::getline(iss, ch, ',')) {
            chs.push_back(std::move(ch));
        }
        return join(std::move(chs));
    }

    void mainloop(
        std::function<void(std::string, std::string, std::string)> callback) {
        auto fn = [=](std::string s) {
            // e.g. => https://www.google.com #
            boost::trim_right(s);
            if (s.back() == '#') {
                return;
            }

            std::string protocol, from, target, url;
            if (parse_privmsg(std::move(s), from, target, protocol, url)) {
                if (target.front() == '#') {
                    // channel mode
                    callback(std::move(protocol), std::move(url), std::move(target));
                } else {
                    // private mode
                    callback(std::move(protocol), std::move(url), std::move(from));
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
