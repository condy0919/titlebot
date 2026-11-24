#ifndef TITLEBOT_UTILS_HPP_
#define TITLEBOT_UTILS_HPP_

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tb {
/// The HTTP/1.1 response
class HttpResponse {
public:
    class Parser {
    public:
        enum class Status {
            Good,
            Bad,
            Indeterminate,
        };

        void reset() {
            state_ = State::http_version_h;
        }

        template <typename Iter>
        std::tuple<Status, Iter> parse(HttpResponse& resp, Iter begin, Iter end) {
            while (begin != end) {
                const Status st = consume(resp, *begin++);
                if (st == Status::Good || st == Status::Bad) {
                    return {st, begin};
                }
            }
            return {Status::Indeterminate, begin};
        }

    private:
        Status consume(HttpResponse& resp, char c);

        enum State {
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
            expecting_newline_3,
        } state_ = http_version_h;
    };

    /// \brief Return the value of key
    /// \require key must be in lower case
    [[nodiscard]] std::optional<std::string> get_header(const std::string& key,
                                                        bool lower_case = true) const;

private:
    int major_;
    int minor_;
    int status_code_;
    std::string desc_;
    std::vector<std::pair<std::string, std::string>> headers_;
};

class HttpChunk {
public:
    class Parser {
    public:
        enum class Status {
            Good,
            Bad,
            Indeterminate,
        };

        template <typename Iter>
        std::tuple<Status, Iter> parse(HttpChunk& chunk, Iter begin, Iter end) {
            while (begin != end) {
                const Status st = consume(chunk, *begin++);
                if (st == Status::Good || st == Status::Bad) {
                    return {st, begin};
                }
            }
            return {Status::Indeterminate, begin};
        }

    private:
        Status consume(HttpChunk& chunk, char c);

        enum State {
            token_start,
            token_size,
            token_extension,
            token_cr1,
            token_lf1,
            token_data,
            token_cr2,
            token_trailer,
            token_trailer_cr,
            token_trailer_lf,
            token_cr3
        } state_ = token_start;
    };

    [[nodiscard]] bool is_last() const {
        return size_ == 0;
    }

    [[nodiscard]] std::size_t size() const {
        return size_;
    }

    [[nodiscard]] const std::vector<char>& data() const {
        return data_;
    }

    void consume() {
        size_ -= data_.size();
        data_.clear();
    }

    void reset() {
        size_ = 0;
        data_.clear();
    }

private:
    std::size_t size_;
    std::vector<char> data_;
};

/// Unquote http stuff.
std::string unquote(const char* s);

/// Make a request to host + uri.
std::string make_request(const std::string& host, const std::string& uri);

/// Convert `in` of any other encoding to utf-8.
std::optional<std::string> utf8(const std::string& in, const std::string& charset = {}, std::string_view sample = {});

/// Parse url and return (schema, host, uri).
std::tuple<std::string, std::string, std::string> parse_url(const std::string& url);

} // namespace tb

#endif // TITLEBOT_UTILS_HPP_
