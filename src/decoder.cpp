#include "decoder.hpp"
#include <iterator>
#include <cassert>

ContentDecoder::ContentDecoder(TitleParser& title_parser)
    : title_parser_(title_parser) {}

std::vector<char> ContentDecoder::decode(const char* s, std::size_t size) {
    return {s, s + size};
}

std::vector<char> ContentDecoder::decode(const char* beg, const char* end) {
    return {beg, end};
}

GzipDecoder::GzipDecoder(TitleParser& title_parser)
    : ContentDecoder(title_parser) {
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    inflateInit2(&strm, 16 + MAX_WBITS);
}

std::vector<char> GzipDecoder::decode(const char* s, std::size_t size) {
    int err = Z_OK;
    char out[2048];
    std::vector<char> ret;
    ret.reserve(512);

    strm.next_in = (unsigned char*)s;
    strm.avail_in = size;
    while (strm.avail_in > 0 && err != Z_STREAM_END) {
        strm.avail_out = sizeof(out);
        strm.next_out = (unsigned char*)out;
        err = inflate(&strm, Z_NO_FLUSH);
        switch (err) {
        case Z_NEED_DICT:
        case Z_DATA_ERROR:
        case Z_MEM_ERROR:
            return {};
        }

        int sz = sizeof(out) - strm.avail_out;
        ret.insert(ret.end(), out, out + sz);
    }
    return ret;
}

std::vector<char> GzipDecoder::decode(const char* beg, const char* end) {
    std::size_t dis = std::distance(beg, end);
    return decode(beg, dis);
}

GzipDecoder::~GzipDecoder() noexcept {
    inflateEnd(&strm);
}

DeflateDecoder::DeflateDecoder(TitleParser& title_parser)
    : ContentDecoder(title_parser) {
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    inflateInit(&strm);
}

std::vector<char> DeflateDecoder::_decode(const char* s, std::size_t size) {
    _decode_ok_ = true;
    int err = Z_OK;
    char out[2048];
    std::vector<char> ret;
    ret.reserve(512);

    strm.next_in = (unsigned char*)s;
    strm.avail_in = size;
    while (strm.avail_in > 0 && err != Z_STREAM_END) {
        strm.avail_out = sizeof(out);
        strm.next_out = (unsigned char*)out;
        err = inflate(&strm, Z_NO_FLUSH);
        switch (err) {
        case Z_NEED_DICT:
        case Z_DATA_ERROR:
        case Z_MEM_ERROR:
            _decode_ok_ = false;
            return {};
        }
        int sz = sizeof(out) - strm.avail_out;
        ret.insert(ret.end(), out, out + sz);
    }
    return ret;
}

std::vector<char> DeflateDecoder::decode(const char* s, std::size_t size) {
    if (!first_try_) {
        return _decode(s, size);
    }

    data_.insert(data_.end(), s, s + size);
    std::vector<char> ret = _decode(s, size);
    if (_decode_ok_) {
        return ret;
    }
    first_try_ = false;
    inflateInit2(&strm, -MAX_WBITS);
    ret = _decode(data_.data(), data_.size());
    if (_decode_ok_) {
        return ret;
    }
    data_.clear();
    data_.shrink_to_fit();
    return {};
}

std::vector<char> DeflateDecoder::decode(const char* beg, const char* end) {
    std::size_t dis = std::distance(beg, end);
    return decode(beg, dis);
}

DeflateDecoder::~DeflateDecoder() noexcept {
    inflateEnd(&strm);
}

ChunkDecoder::ChunkDecoder(std::shared_ptr<ContentDecoder> content_decoder)
    : content_decoder_(content_decoder) {}

void ChunkDecoder::setParser(std::unique_ptr<Http::Chunk::Parser> parser) {
    impl_ = std::move(parser);
}
