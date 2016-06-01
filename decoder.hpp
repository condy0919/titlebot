#pragma once

#include "utils/http.hpp"
#include "title_parser.hpp"
#include <zlib.h>
#include <vector>
#include <memory>

class ContentDecoder {
public:
    ContentDecoder(TitleParser& title_parser);

    virtual std::vector<char> decode(const char* s, std::size_t size);
    virtual std::vector<char> decode(const char* beg, const char* end);

    template <typename Iter>
    bool parse(Iter beg, Iter end) {
        std::vector<char> res = decode(beg, end);
        return title_parser_.parse(res.begin(), res.end());
    }

    virtual ~ContentDecoder() noexcept {}

private:
    TitleParser& title_parser_;
};

class GzipDecoder : public ContentDecoder {
public:
    GzipDecoder(TitleParser& title_parser);

    std::vector<char> decode(const char* s, std::size_t size) override;
    std::vector<char> decode(const char* beg, const char* end) override;

    ~GzipDecoder() noexcept override;

private:
    z_stream strm;
};

class DeflateDecoder : public ContentDecoder {
public:
    DeflateDecoder(TitleParser& title_parser);

    std::vector<char> decode(const char* s, std::size_t size) override;
    std::vector<char> decode(const char* beg, const char* end) override;

    ~DeflateDecoder() noexcept override;

private:
    z_stream strm;
};


class ChunkDecoder {
public:
    ChunkDecoder(std::shared_ptr<ContentDecoder> content_decoder);

    void setParser(std::unique_ptr<Http::Chunk::Parser> parser);

    template <typename Iter>
    bool parse(Iter beg, Iter end) {
        std::size_t len = 0;
        Iter iter = nullptr;

        while (beg != end) {
            if (impl_) {
                Http::Chunk::Parser::state st;
                std::tie(st, beg) = impl_->parse(chunk_, beg, end);
                if ((st == Http::Chunk::Parser::good &&
                     !chunk_.isLastChunk()) ||
                    st == Http::Chunk::Parser::indeterminate) {
                    len = chunk_.data_.size();
                    iter = chunk_.data_.data();
                } else {
                    // last chunk or bad format, either ends chunk
                    return false;
                }
            } else {
                len = std::distance(beg, end);
                iter = beg;
                beg = end;
            }

            bool r = content_decoder_->parse(iter, iter + len);
            if (r) {
                return true;
            }
            chunk_.consumeData();
        }
        return false;
    }

private:
    std::shared_ptr<ContentDecoder> content_decoder_;
    std::unique_ptr<Http::Chunk::Parser> impl_;
    Http::Chunk chunk_;
};
