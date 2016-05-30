#pragma once

#include <tuple>
#include <string>
#include <vector>
#include <utility>

namespace Http {
struct Request {
    // host => www.acgdoge.net, uri => /archives/1234
    static std::string get(const std::string& host, const std::string& uri);
};

struct Response {
    int major_;
    int minor_;
    int status_code_;
    std::string desc;
    std::vector<std::pair<std::string, std::string>> headers;

    std::string getHeader(std::string key) const;

    struct Parser {
        enum state { good, bad, indeterminate };

        Parser();

        void reset();

        template <typename Iter>
        std::tuple<state, Iter> parse(Response& resp, Iter beg, Iter end) {
            while (beg != end) {
                state st = consume(resp, *beg++);
                if (st == good || st == bad) {
                    return std::make_tuple(st, beg);
                }
            }
            return std::make_tuple(indeterminate, beg);
        }

    private:
        state consume(Response& resp, char c);

        enum internal_state {
            http_version_h,
            http_version_t_1,
            http_version_t_2,
            http_version_p,
            http_version_slash,
            http_version_major_start,
            http_version_major,
            http_version_minor_start,
            http_version_minor,
            http_status_code,
            http_status_msg,
            expecting_newline_1,
            header_line_start,
            header_lws,
            header_name,
            space_before_header_value,
            header_value,
            expecting_newline_2,
            expecting_newline_3
        } state_;
    };
};

struct Chunk {
    std::size_t size_;
    std::vector<char> data_;

    void reset();

    bool isLastChunk() const;

    struct Parser {
        enum state { good, bad, indeterminate };

        Parser();

        template <typename Iter>
        std::tuple<state, Iter> parse(Chunk& chunk_, Iter beg, Iter end) {
            while (beg != end) {
                state st = consume(chunk_, *beg++);
                if (st == good || st == bad) {
                    return std::make_tuple(st, beg);
                }
            }
            return std::make_tuple(indeterminate, beg);
        }

    private:
        state consume(Chunk& chunk_, char c);

        enum internal_state {
            TOKEN_START,
            TOKEN_SIZE,
            TOKEN_EXTENSION,
            TOKEN_CR1,
            TOKEN_LF1,
            TOKEN_DATA,
            TOKEN_CR2,
            TOKEN_TRAILER,
            TOKEN_TRAILER_CR,
            TOKEN_TRAILER_LF,
            TOKEN_CR3
        } state_;
    };
};
}
