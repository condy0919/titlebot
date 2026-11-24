#ifndef TITLEBOT_DECODER_HPP_
#define TITLEBOT_DECODER_HPP_

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "html/title_parser.hpp"

namespace tb {
enum class ContentEncoding : std::uint8_t {
    kIdentity,
    kGzip,
    kDeflate,
    kRawDeflate,
};

enum class DecodeError : std::uint8_t {
    kInvalid,
    kLimit,
};

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
    struct InflateState;

    ContentEncoding encoding_;
    std::unique_ptr<InflateState> inflater_;
    std::size_t remaining_ = 512UL * 1024;
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

#endif // TITLEBOT_DECODER_HPP_
