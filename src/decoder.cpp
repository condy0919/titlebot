#include "decoder.hpp"

#include <utility>
#include <vector>

namespace tb {
GzipDecoder::GzipDecoder() {
    strm_.zalloc = Z_NULL;
    strm_.zfree = Z_NULL;
    strm_.opaque = Z_NULL;
    strm_.avail_in = 0;
    strm_.next_in = Z_NULL;
    ::inflateInit2(&strm_, 16 + MAX_WBITS);
}

std::vector<char> GzipDecoder::decode(const char* s, std::size_t size) {
    data_.clear();

    int err = Z_OK;
    char out[2048];
    strm_.next_in = (unsigned char*)s;
    strm_.avail_in = size;
    while (strm_.avail_in > 0 && err != Z_STREAM_END) {
        strm_.avail_out = sizeof(out);
        strm_.next_out = (unsigned char*)out;
        err = ::inflate(&strm_, Z_NO_FLUSH);
        switch (err) {
        case Z_NEED_DICT:
        case Z_DATA_ERROR:
        case Z_MEM_ERROR:
            return {};
        }

        const int sz = static_cast<int>(sizeof(out) - strm_.avail_out);
        data_.insert(data_.end(), out, out + sz);
    }
    return data_;
}

GzipDecoder::~GzipDecoder() {
    ::inflateEnd(&strm_);
}

DeflateDecoder::DeflateDecoder() {
    strm_.zalloc = Z_NULL;
    strm_.zfree = Z_NULL;
    strm_.opaque = Z_NULL;
    strm_.avail_in = 0;
    strm_.next_in = Z_NULL;
    ::inflateInit2(&strm_, -MAX_WBITS);
}

std::vector<char> DeflateDecoder::decode(const char* s, std::size_t size) {
    data_.clear();

    int err = Z_OK;
    char out[2048];
    strm_.next_in = (unsigned char*)s;
    strm_.avail_in = size;
    while (strm_.avail_in > 0 && err != Z_STREAM_END) {
        strm_.avail_out = sizeof(out);
        strm_.next_out = (unsigned char*)out;
        err = inflate(&strm_, Z_NO_FLUSH);
        switch (err) {
        case Z_NEED_DICT:
        case Z_DATA_ERROR:
        case Z_MEM_ERROR:
            return {};
        }

        const int sz = static_cast<int>(sizeof(out) - strm_.avail_out);
        data_.insert(data_.end(), out, out + sz);
    }
    return data_;
}

DeflateDecoder::~DeflateDecoder() {
    ::inflateEnd(&strm_);
}
} // namespace tb
