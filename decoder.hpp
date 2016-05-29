#pragma once

#include <zlib.h>
#include <vector>

class Decoder {
public:
    virtual std::vector<unsigned char> decode(const unsigned char* s,
                                              std::size_t size);
    virtual std::vector<unsigned char> decode(const unsigned char* beg,
                                              const unsigned char* end);

    virtual ~Decoder() {}
};

class GzipDecoder : public Decoder {
public:
    GzipDecoder();

    std::vector<unsigned char> decode(const unsigned char* s,
                                      std::size_t size) override;
    std::vector<unsigned char> decode(const unsigned char* beg,
                                      const unsigned char* end) override;

    ~GzipDecoder() override;

private:
    z_stream strm;
};

class DeflateDecoder : public Decoder {
public:
    DeflateDecoder();

    std::vector<unsigned char> decode(const unsigned char* s,
                                      std::size_t size) override;
    std::vector<unsigned char> decode(const unsigned char* beg,
                                      const unsigned char* end) override;

    ~DeflateDecoder() override;

private:
    z_stream strm;
};
