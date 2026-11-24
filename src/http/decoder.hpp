#ifndef TITLEBOT_DECODER_HPP_
#define TITLEBOT_DECODER_HPP_

#include "html/title_parser.hpp"

#include <iterator>
#include <memory>
#include <optional>
#include <vector>

#include "common/utils.hpp"

#include <zlib.h>

namespace tb {
/// The base class of all Decoders.
class ContentDecoder {
public:
    template <typename Iter>
    std::optional<std::string> parse(TitleParser& tparser, Iter beg, Iter end) {
        const std::vector<char>& res = decode(beg, end);
        return tparser.parse(res.begin(), res.end());
    }

    virtual ~ContentDecoder() = default;

private:
    virtual std::vector<char> decode(const char* s, std::size_t size) {
        return {s, s + size};
    }

    virtual std::vector<char> decode(const char* begin, const char* end) {
        return {begin, end};
    }
};

///
class GzipDecoder : public ContentDecoder {
public:
    GzipDecoder();

    ~GzipDecoder() override;

private:
    std::vector<char> decode(const char* s, std::size_t size) override;

    std::vector<char> decode(const char* begin, const char* end) override {
        const std::size_t len = std::distance(begin, end);
        return decode(begin, len);
    }

    z_stream strm_;
    std::vector<char> data_;
};

///
class DeflateDecoder : public ContentDecoder {
public:
    DeflateDecoder();

    ~DeflateDecoder() override;

private:
    std::vector<char> decode(const char* s, std::size_t size) override;

    std::vector<char> decode(const char* begin, const char* end) override {
        const std::size_t len = std::distance(begin, end);
        return decode(begin, len);
    }

    z_stream strm_;
    std::vector<char> data_;
};
} // namespace tb

#endif // TITLEBOT_DECODER_HPP_
