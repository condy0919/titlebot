#include "http.hpp"

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
}

namespace Http {
// host => www.acgdoge.net, uri => /archives/1234
std::string Request::get(const std::string& host, const std::string& uri) {
    std::string req;
    req.reserve(512);

    req += "GET " + uri + " HTTP/1.1\r\n";
    req += "Accept: */*\r\n";
    req += "Accept-Encoding: */*\r\n";
    //req += "Accept-Encoding: gzip, deflate\r\n";
    req += "Host: " + host + "\r\n";
    req += "User-Agent: titlebot\r\n";
    req += "Connection: close\r\n";
    req += "\r\n";

    return req;
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
            resp.headers.back().first.push_back(input);
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
            resp.headers.back().first.push_back(input);
            return indeterminate;
        }

    case space_before_header_value:
        if (input == ' ') {
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
}
