#ifndef TITLEBOT_DECODER_HPP_
#define TITLEBOT_DECODER_HPP_
#include "html/title_parser.hpp"
#include <expected>
#include <string_view>
#include <zlib.h>

namespace tb {
enum class ContentEncoding { kIdentity, kGzip, kDeflate, kRawDeflate };
enum class DecodeError { kInvalid, kLimit };

class ContentDecoder {
public:
    explicit ContentDecoder(ContentEncoding encoding = ContentEncoding::kIdentity);
    virtual ~ContentDecoder();
    ContentDecoder(const ContentDecoder&) = delete;
    ContentDecoder& operator=(const ContentDecoder&) = delete;

    std::expected<std::optional<std::string>, DecodeError>
    Consume(TitleParser& parser, std::string_view input, bool final);

    std::optional<std::string> parse(TitleParser& parser, const char* begin, const char* end) {
        return Consume(parser, {begin, end}, false).value_or(std::nullopt);
    }

private:
    ContentEncoding encoding_;
    z_stream stream_{};
    bool initialized_ = false;
    bool ended_ = false;
    std::size_t remaining_ = 512 * 1024;
};

class GzipDecoder : public ContentDecoder {
public:
    GzipDecoder() : ContentDecoder(ContentEncoding::kGzip) {}
};

class DeflateDecoder : public ContentDecoder {
public:
    DeflateDecoder() : ContentDecoder(ContentEncoding::kRawDeflate) {}
};
} // namespace tb
#endif
