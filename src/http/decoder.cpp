#include "decoder.hpp"
#include <algorithm>
#include <array>
#include <limits>

namespace tb {
ContentDecoder::ContentDecoder(ContentEncoding encoding) : encoding_(encoding) {
    if (encoding != ContentEncoding::kIdentity) {
        int bits = encoding == ContentEncoding::kGzip ? MAX_WBITS + 16 : MAX_WBITS;
        if (encoding == ContentEncoding::kRawDeflate) {
            bits = -MAX_WBITS;
        }
        initialized_ = inflateInit2(&stream_, bits) == Z_OK;
    }
}

ContentDecoder::~ContentDecoder() {
    if (initialized_) {
        inflateEnd(&stream_);
    }
}

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
    if (!initialized_ || input.size() > std::numeric_limits<uInt>::max()) {
        return std::unexpected(DecodeError::kInvalid);
    }
    std::string joined;
    if (encoding_ == ContentEncoding::kDeflate && !checked_wrapper_) {
        if (!prefix_.empty()) {
            joined = std::move(prefix_);
            joined.append(input);
            input = joined;
        }
        if (input.size() < 2 && !final) {
            prefix_ = input;
            return std::nullopt;
        }
        if (input.size() < 2) {
            return std::unexpected(DecodeError::kInvalid);
        }
        const auto cmf = static_cast<unsigned char>(input[0]);
        const auto flg = static_cast<unsigned char>(input[1]);
        if ((cmf & 15) != Z_DEFLATED || cmf > 120 || ((cmf << 8) + flg) % 31 != 0) {
            if (inflateReset2(&stream_, -MAX_WBITS) != Z_OK) {
                return std::unexpected(DecodeError::kInvalid);
            }
        }
        checked_wrapper_ = true;
    }
    stream_.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream_.avail_in = static_cast<uInt>(input.size());
    std::array<char, 4096> output;
    while (!ended_ || (encoding_ == ContentEncoding::kGzip && stream_.avail_in)) {
        if (ended_) {
            if (inflateReset(&stream_) != Z_OK) {
                return std::unexpected(DecodeError::kInvalid);
            }
            ended_ = false;
        }
        stream_.next_out = reinterpret_cast<Bytef*>(output.data());
        stream_.avail_out = output.size();
        const int status = inflate(&stream_, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END && status != Z_BUF_ERROR) {
            return std::unexpected(DecodeError::kInvalid);
        }
        ended_ = status == Z_STREAM_END;
        auto title = feed({output.data(), output.size() - stream_.avail_out});
        if (!title || *title) {
            return title;
        }
        if (!stream_.avail_in && stream_.avail_out) {
            break;
        }
        if (status == Z_BUF_ERROR) {
            return std::unexpected(DecodeError::kInvalid);
        }
    }
    if ((final && !ended_) || stream_.avail_in) {
        return std::unexpected(DecodeError::kInvalid);
    }
    return std::nullopt;
}
} // namespace tb
