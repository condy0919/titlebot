#include "http.hpp"
#include "string.hpp"
#include <cctype>
#include <boost/algorithm/string.hpp>

namespace {
bool is_char(int c) {
    return c >= 0 && c <= 127;
}

bool is_ctl(int c) {
    return (c >= 0 && c <= 31) || (c == 127);
}

bool is_tspecial(int c) {
    switch (c) {
    case '(':
    case ')':
    case '<':
    case '>':
    case '@':
    case ',':
    case ';':
    case ':':
    case '\\':
    case '"':
    case '/':
    case '[':
    case ']':
    case '?':
    case '=':
    case '{':
    case '}':
    case ' ':
    case '\t':
        return true;
    default:
        return false;
    }
}

bool is_digit(int c) {
    return c >= '0' && c <= '9';
}

int tohex(int c) {
    if (is_digit(c)) {
        return c - '0';
    } else if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    } else {
        return c - 'A' + 10;
    }
}
}

namespace Http {
// host => www.acgdoge.net, uri => /archives/1234
std::string Request::get(const std::string& host, const std::string& uri) {
    std::string req;
    req.reserve(256);

    StringUtils::concat(req, "GET ", uri, " HTTP/1.1\r\n");
    StringUtils::concat(req, "Accept: */*\r\n");
    StringUtils::concat(req, "Accept-Encoding: gzip,deflate,identity\r\n");
    StringUtils::concat(req, "Accept-Language: zh-cn,en\r\n");
    StringUtils::concat(req, "Host: ", host, "\r\n");
    StringUtils::concat(req, "User-Agent: titlebot\r\n");
    StringUtils::concat(req, "Connection: close\r\n\r\n");

    return req;
}

/*
 * @require `key' must be in lower case
 */
std::string Response::getHeader(std::string key, bool lower_case) const {
    for (auto pair_ : headers) {
        if (pair_.first == key) {
            if (lower_case) {
                std::string& s = pair_.second;
                std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            }
            return pair_.second;
        }
    }
    return {};
}

Response::Parser::Parser() : state_(http_version_h) {}

void Response::Parser::reset() {
    state_ = http_version_h;
}

Response::Parser::state Response::Parser::consume(Response& resp, char input) {
    switch (state_) {
    case http_version_h:
        if (input == 'H') {
            state_ = http_version_t_1;
            return indeterminate;
        } else {
            return bad;
        }

    case http_version_t_1:
        if (input == 'T') {
            state_ = http_version_t_2;
            return indeterminate;
        } else {
            return bad;
        }

    case http_version_t_2:
        if (input == 'T') {
            state_ = http_version_p;
            return indeterminate;
        } else {
            return bad;
        }

    case http_version_p:
        if (input == 'P') {
            state_ = http_version_slash;
            return indeterminate;
        } else {
            return bad;
        }

    case http_version_slash:
        if (input == '/') {
            resp.major_ = resp.minor_ = 0;
            state_ = http_version_major_start;
            return indeterminate;
        } else {
            return bad;
        }

    case http_version_major_start:
        if (is_digit(input)) {
            resp.major_ = resp.major_ * 10 + (input - '0');
            state_ = http_version_major;
            return indeterminate;
        } else {
            return bad;
        }

    case http_version_major:
        if (input == '.') {
            state_ = http_version_minor_start;
            return indeterminate;
        } else if (is_digit(input)) {
            resp.major_ = resp.major_ * 10 + (input - '0');
            return indeterminate;
        } else {
            return bad;
        }

    case http_version_minor_start:
        if (is_digit(input)) {
            resp.minor_ = resp.minor_ * 10 + (input - '0');
            state_ = http_version_minor;
            return indeterminate;
        } else {
            return bad;
        }

    case http_version_minor:
        if (input == ' ') {
            resp.status_code_ = 0;
            state_ = http_status_code;
            return indeterminate;
        } else if (is_digit(input)) {
            resp.minor_ = resp.minor_ * 10 + (input - '0');
            return indeterminate;
        } else {
            return bad;
        }

    case http_status_code:
        if (is_digit(input)) {
            resp.status_code_ = resp.status_code_ * 10 + (input - '0');
            return indeterminate;
        } else if (input == ' ') {
            state_ = http_status_msg;
            return indeterminate;
        } else {
            return bad;
        }

    case http_status_msg:
        if (input == '\r') {
            state_ = expecting_newline_1;
            return indeterminate;
        } else if (is_char(input)) {
            resp.desc.push_back(input);
            return indeterminate;
        } else {
            return bad;
        }

    case expecting_newline_1:
        if (input == '\n') {
            state_ = header_line_start;
            return indeterminate;
        } else {
            return bad;
        }

    case header_line_start:
        if (input == '\r') {
            state_ = expecting_newline_3;
            return indeterminate;
        } else if (!resp.headers.empty() && (input == ' ' || input == '\t')) {
            state_ = header_lws;
            return indeterminate;
        } else if (!is_char(input) || is_ctl(input) || is_tspecial(input)) {
            return bad;
        } else {
            resp.headers.emplace_back();
            resp.headers.back().first.push_back(std::tolower(input));
            state_ = header_name;
            return indeterminate;
        }

    case header_lws:
        if (input == '\r') {
            state_ = expecting_newline_2;
            return indeterminate;
        } else if (input == ' ' || input == '\t') {
            return indeterminate;
        } else if (is_ctl(input)) {
            return bad;
        } else {
            state_ = header_value;
            resp.headers.back().second.push_back(input);
            return indeterminate;
        }

    case header_name:
        if (input == ':') {
            state_ = space_before_header_value;
            return indeterminate;
        } else if (!is_char(input) || is_ctl(input) || is_tspecial(input)) {
            return bad;
        } else {
            resp.headers.back().first.push_back(std::tolower(input));
            return indeterminate;
        }

    case space_before_header_value:
        if (input == ' ') {
            return indeterminate;
        } else if (input == '\r') {
            state_ = expecting_newline_2;
            return indeterminate;
        } else if (::isascii(input)) {
            resp.headers.back().second.push_back(input);
            state_ = header_value;
            return indeterminate;
        } else {
            return bad;
        }

    case header_value:
        if (input == '\r') {
            state_ = expecting_newline_2;
            return indeterminate;
        } else if (is_ctl(input)) {
            return bad;
        } else {
            resp.headers.back().second.push_back(input);
            return indeterminate;
        }

    case expecting_newline_2:
        if (input == '\n') {
            state_ = header_line_start;
            return indeterminate;
        } else {
            return bad;
        }

    case expecting_newline_3:
        return (input == '\n') ? good : bad;

    default:
        return bad;
    }
}

void Chunk::consumeData() {
    size_ -= data_.size();
    data_.clear();
}

void Chunk::reset() {
    size_ = 0;
    data_.clear();
}

bool Chunk::isLastChunk() const {
    return !size_;
}

Chunk::Parser::Parser() : state_(TOKEN_START) {}

Chunk::Parser::state Chunk::Parser::consume(Chunk& chunk_, char c) {
    switch (state_) {
    case TOKEN_START:
        chunk_.data_.clear();
        if (std::isxdigit(c)) {
            chunk_.size_ = tohex(c);
            state_ = TOKEN_SIZE;
            return indeterminate;
        } else {
            return bad;
        }

    case TOKEN_SIZE:
        if (std::isxdigit(c)) {
            chunk_.size_ = 16 * chunk_.size_ + tohex(c);
            return indeterminate;
        } else if (c == '\r') {
            state_ = TOKEN_CR1;
            return indeterminate;
        } else {
            state_ = TOKEN_EXTENSION;
            return indeterminate;
        }

    case TOKEN_EXTENSION:
        if (c == '\r') {
            state_ = TOKEN_CR1;
            return indeterminate;
        } else {
            return indeterminate;
        }

    case TOKEN_CR1:
        if (c == '\n') {
            state_ = TOKEN_LF1;
            return indeterminate;
        } else {
            return bad;
        }

    case TOKEN_LF1:
        if (chunk_.size_ == 0) {
            if (c == '\r') {
                state_ = TOKEN_CR3;
            } else {
                state_ = TOKEN_TRAILER;
            }
            return indeterminate;
        } else {
            chunk_.data_.push_back(c);
            state_ = TOKEN_DATA;
            return indeterminate;
        } 

    case TOKEN_DATA:
        if (chunk_.data_.size() < chunk_.size_) {
            chunk_.data_.push_back(c);
            return indeterminate;
        } else if (chunk_.data_.size() == chunk_.size_ && c == '\r') {
            state_ = TOKEN_CR2;
            return indeterminate;
        } else {
            return bad;
        }

    case TOKEN_CR2:
        if (c == '\n') {
            state_ = TOKEN_START;
            return good;
        } else {
            return bad;
        }

    case TOKEN_TRAILER:
        if (c == '\r') {
            state_ = TOKEN_TRAILER_CR;
            return indeterminate;
        } else {
            return indeterminate;
        }

    case TOKEN_TRAILER_CR:
        if (c == '\n') {
            state_ = TOKEN_TRAILER_LF;
            return indeterminate;
        } else {
            return bad;
        }

    case TOKEN_TRAILER_LF:
        if (c == '\r') {
            state_ = TOKEN_CR3;
            return indeterminate;
        } else {
            state_ = TOKEN_TRAILER;
            return indeterminate;
        }

    case TOKEN_CR3:
        if (c == '\n') {
            return good;
        } else {
            return bad;
        }

    default:
        return bad;
    }
}

std::tuple<std::string, std::string, std::string> parseURL(std::string url) {
    std::string schema, host, uri;

    const static char delim[] = {':', '/', '/'};
    auto colon_start = std::search(url.begin(), url.end(), std::begin(delim), std::end(delim));
    if (colon_start != url.end()) {
        schema = std::string(url.begin(), colon_start);
        std::advance(colon_start, 3);
    } else {
        colon_start = url.begin();
    }

    host = std::string(colon_start, url.end()); // :// => 3
    uri = "/";
    auto iter = std::find(host.begin(), host.end(), '/');
    if (iter != host.end()) {
        uri = std::string(iter, host.end());
        host.erase(iter, host.end());
    }

    iter = std::find(uri.begin(), uri.end(), '#');
    if (iter != uri.end()) {
        uri.erase(iter, uri.end());
    }

    return std::make_tuple(std::move(schema), std::move(host),
                           std::move(uri));
}
}
