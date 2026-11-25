#ifndef TITLEBOT_DOCUMENT_BYTES_HPP_
#define TITLEBOT_DOCUMENT_BYTES_HPP_

#include <array>
#include <string>
#include <string_view>

#include <boost/locale/utf.hpp>

namespace tb {
/// Decode BOM-marked UTF-16 incrementally and otherwise pass bytes through.
class DocumentBytes {
public:
    std::string_view Push(char ch) {
        if (sample_.size() < 1024) {
            sample_.push_back(ch);
        }
        if (sample_.size() == 1 && (ch == '\xff' || ch == '\xfe')) {
            return {};
        }
        if (sample_.size() == 2 && (sample_[0] == '\xff' || sample_[0] == '\xfe')) {
            if (sample_ == "\xff\xfe" || sample_ == "\xfe\xff") {
                utf16_ = true;
                little_endian_ = sample_[0] == '\xff';
                return {};
            }
            return sample_;
        }
        if (!utf16_) {
            bytes_[0] = ch;
            return {bytes_.data(), 1};
        }
        unsigned byte = static_cast<unsigned char>(ch);
        if (first_ < 0) {
            first_ = byte;
            return {};
        }
        unsigned unit = little_endian_ ? first_ | (byte << 8) : (first_ << 8) | byte;
        first_ = -1;
        auto out = bytes_.data();
        if (high_) {
            if (unit >= 0xdc00 && unit <= 0xdfff) {
                unit = 0x10000 + ((high_ - 0xd800) << 10) + unit - 0xdc00;
            } else {
                out = boost::locale::utf::utf_traits<char>::encode(0xfffd, out);
            }
            high_ = 0;
        }
        if (unit >= 0xd800 && unit <= 0xdbff) {
            high_ = unit;
        } else {
            out = boost::locale::utf::utf_traits<char>::encode(
                unit >= 0xdc00 && unit <= 0xdfff ? 0xfffd : unit, out);
        }
        return {bytes_.data(), static_cast<std::size_t>(out - bytes_.data())};
    }

    std::string_view sample() const { return sample_; }
    bool has_bom() const { return utf16_ || sample_.starts_with("\xef\xbb\xbf"); }
    bool can_skip_bytes() const { return !utf16_ && sample_.size() == 1024; }

private:
    std::string sample_;
    std::array<char, 8> bytes_{};
    int first_ = -1;
    unsigned high_ = 0;
    bool utf16_ = false;
    bool little_endian_ = false;
};
} // namespace tb

#endif // TITLEBOT_DOCUMENT_BYTES_HPP_
