#ifndef TITLEBOT_DECODER_HPP_
#define TITLEBOT_DECODER_HPP_

#include <cstddef>
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
    /// Select the body encoding for one response. A default-constructed decoder reads plain HTML.
    explicit ContentDecoder(ContentEncoding encoding = ContentEncoding::kIdentity);

    virtual ~ContentDecoder();

    ContentDecoder(const ContentDecoder&) = delete;
    ContentDecoder& operator=(const ContentDecoder&) = delete;

    /// Decode one body chunk, setting final for the last chunk. A successful empty optional
    /// means no complete title was found. Invalid data and size limits produce DecodeError.
    /// For example, on a fresh identity decoder, Consume(parser, "<title>OK</title>", true)
    /// yields a successful result containing "OK".
    std::expected<std::optional<std::string>, DecodeError>
    Consume(TitleParser& parser, std::string_view input, bool final);

    /// Feed a non-final range, returning std::nullopt for both missing titles and decode errors.
    /// For example, with html = "<title>OK</title>" in a std::string, an identity decoder's
    /// parse(parser, html.data(), html.data() + html.size()) yields "OK".
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

/// Decode raw DEFLATE data without a zlib wrapper.
class DeflateDecoder : public ContentDecoder {
public:
    DeflateDecoder() : ContentDecoder(ContentEncoding::kRawDeflate) {}
};
} // namespace tb

#endif // TITLEBOT_DECODER_HPP_
