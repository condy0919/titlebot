#include "decoder.hpp"
#include <cassert>

std::string ContentDecoder::decode(const char* s, std::size_t size) {
    return std::string(s, size);
}

GzipDecoder::GzipDecoder() {
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    inflateInit2(&strm, 16 + MAX_WBITS);
}

std::string GzipDecoder::decode(const char* s, std::size_t size) {
    int err = Z_OK;
    unsigned char out[2048];
    std::string ret;
    ret.reserve(512);

    strm.next_in = (unsigned char*)s;
    strm.avail_in = size;
    while (strm.avail_in > 0 && err != Z_STREAM_END) {
        strm.avail_out = sizeof(out);
        strm.next_out = out;
        err = inflate(&strm, Z_NO_FLUSH);
        switch (err) {
        case Z_NEED_DICT:
        case Z_DATA_ERROR:
        case Z_MEM_ERROR:
            throw "data error";
        }

        int sz = sizeof(out) - strm.avail_out;
        ret.append((char*)out, sz);
    }
    return ret;
}

GzipDecoder::~GzipDecoder() noexcept {
    inflateEnd(&strm);
}

DeflateDecoder::DeflateDecoder() {
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    inflateInit(&strm);
}

std::string DeflateDecoder::decode(const char* s, std::size_t size) {
    int err = Z_OK;
    unsigned char out[2048];
    std::string ret;
    ret.reserve(512);

    strm.next_in = (unsigned char*)s;
    strm.avail_in = size;
    while (strm.avail_in > 0 && err != Z_STREAM_END) {
        strm.avail_out = sizeof(out);
        strm.next_out = out;
        err = inflate(&strm, Z_NO_FLUSH);
        switch (err) {
        case Z_NEED_DICT:
        case Z_DATA_ERROR:
        case Z_MEM_ERROR:
            throw "data error";
        }
        int sz = sizeof(out) - strm.avail_out;
        ret.append((char*)out, sz);
    }
    return ret;
}

DeflateDecoder::~DeflateDecoder() noexcept {
    inflateEnd(&strm);
}
