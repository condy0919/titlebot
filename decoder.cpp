#include "decoder.hpp"
#include <iterator>
#include <cassert>

std::vector<unsigned char> Decoder::decode(const unsigned char* s,
                                           std::size_t size) {
    return {s, s + size};
}

std::vector<unsigned char> Decoder::decode(const unsigned char* beg,
                                           const unsigned char* end) {
    return {beg, end};
}

GzipDecoder::GzipDecoder() {
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    inflateInit2(&strm, 16 + MAX_WBITS);
}

std::vector<unsigned char> GzipDecoder::decode(const unsigned char* s,
                                               std::size_t size) {
    int err = Z_OK;
    unsigned char out[2048];
    std::vector<unsigned char> ret;
    ret.reserve(512);

    strm.next_in = const_cast<unsigned char*>(s);
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
        ret.insert(ret.end(), out, out + sz);
    }
    return ret;
}

std::vector<unsigned char> GzipDecoder::decode(const unsigned char* beg,
                                               const unsigned char* end) {
    std::size_t dis = std::distance(beg, end);
    return decode(beg, dis);
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

std::vector<unsigned char> DeflateDecoder::decode(const unsigned char* s,
                                                  std::size_t size) {
    int err = Z_OK;
    unsigned char out[2048];
    std::vector<unsigned char> ret;
    ret.reserve(512);

    strm.next_in = const_cast<unsigned char*>(s);
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
        ret.insert(ret.end(), out, out + sz);
    }
    return ret;
}

std::vector<unsigned char> DeflateDecoder::decode(const unsigned char* beg,
                                                  const unsigned char* end) {
    std::size_t dis = std::distance(beg, end);
    return decode(beg, dis);
}

DeflateDecoder::~DeflateDecoder() noexcept {
    inflateEnd(&strm);
}
