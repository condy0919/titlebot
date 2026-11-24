#include "decoder.hpp"

#include <array>
#include <limits>
#include <zlib.h>

namespace tb {
struct ContentDecoder::InflateState {
    explicit InflateState(int bits) : initialized(inflateInit2(&stream, bits) == Z_OK) {}

    ~InflateState() {
        if (initialized) {
            inflateEnd(&stream);
        }
    }

    z_stream stream{};
    bool initialized;
    std::string prefix;
    bool checked_wrapper = false;
    bool ended = false;
};

ContentDecoder::ContentDecoder(ContentEncoding encoding) : encoding_(encoding) {
    if (encoding != ContentEncoding::kIdentity) {
        int bits = encoding == ContentEncoding::kGzip ? MAX_WBITS + 16 : MAX_WBITS;
        if (encoding == ContentEncoding::kRawDeflate) {
            bits = -MAX_WBITS;
        }
        inflater_ = std::make_unique<InflateState>(bits);
    }
}

ContentDecoder::~ContentDecoder() = default;

std::expected<std::optional<std::string>, DecodeError>
ContentDecoder::Consume(TitleParser& parser, std::string_view input, bool final) {
    auto feed =
        [&](std::string_view bytes) -> std::expected<std::optional<std::string>, DecodeError> {
        auto prefix = bytes.substr(0, remaining_);
        remaining_ -= prefix.size();
        auto title = parser.parse(prefix.begin(), prefix.end());
        if (!title && bytes.size() != prefix.size()) {
            return std::unexpected(DecodeError::kLimit);
        }
        return title;
    };
    if (encoding_ == ContentEncoding::kIdentity) {
        return feed(input);
    }
    auto& state = *inflater_;
    auto& stream = state.stream;
    if (!state.initialized || input.size() > std::numeric_limits<uInt>::max()) {
        return std::unexpected(DecodeError::kInvalid);
    }
    std::string joined;
    if (encoding_ == ContentEncoding::kDeflate && !state.checked_wrapper) {
        if (!state.prefix.empty()) {
            joined = std::move(state.prefix);
            joined.append(input);
            input = joined;
        }
        if (input.size() < 2 && !final) {
            state.prefix = input;
            return std::nullopt;
        }
        if (input.size() < 2) {
            return std::unexpected(DecodeError::kInvalid);
        }
        const auto cmf = static_cast<unsigned char>(input[0]);
        const auto flg = static_cast<unsigned char>(input[1]);
        if ((cmf & 15) != Z_DEFLATED || cmf > 120 || ((cmf << 8) + flg) % 31 != 0) {
            if (inflateReset2(&stream, -MAX_WBITS) != Z_OK) {
                return std::unexpected(DecodeError::kInvalid);
            }
        }
        state.checked_wrapper = true;
    }
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    std::array<char, 4096> output;
    while (!state.ended || (encoding_ == ContentEncoding::kGzip && stream.avail_in)) {
        if (state.ended) {
            if (inflateReset(&stream) != Z_OK) {
                return std::unexpected(DecodeError::kInvalid);
            }
            state.ended = false;
        }
        stream.next_out = reinterpret_cast<Bytef*>(output.data());
        stream.avail_out = output.size();
        const int status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) {
            return std::unexpected(DecodeError::kInvalid);
        }
        state.ended = status == Z_STREAM_END;
        auto title = feed({output.data(), output.size() - stream.avail_out});
        if (!title || *title) {
            return title;
        }
        if (!stream.avail_in && stream.avail_out) {
            break;
        }
        if (status == Z_BUF_ERROR) {
            return std::unexpected(DecodeError::kInvalid);
        }
    }
    if ((final && !state.ended) || stream.avail_in) {
        return std::unexpected(DecodeError::kInvalid);
    }
    return std::nullopt;
}
} // namespace tb
