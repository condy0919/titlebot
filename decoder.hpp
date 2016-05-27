#pragma once

#include <zlib.h>
#include <string>

class ContentDecoder {
public:
    virtual std::string decode(const char* s, std::size_t size);

    virtual ~ContentDecoder() {}
};

class GzipDecoder : public ContentDecoder {
public:
    GzipDecoder();

    std::string decode(const char* s, std::size_t size) override;

    ~GzipDecoder() override;

private:
    z_stream strm;
};

class DeflateDecoder : public ContentDecoder {
public:
    DeflateDecoder();

    std::string decode(const char* s, std::size_t size) override;

    ~DeflateDecoder() override;

private:
    z_stream strm;
};
