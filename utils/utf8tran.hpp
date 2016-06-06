#pragma once

#include "iconv.hpp"
#include "chardet.hpp"
#include <memory>
#include <cstring>

class UTF8Translator {
public:
    UTF8Translator() = default;

    std::string trans(std::string in) {
        bool b = det_.feed(in.data(), in.size());
        if (!b) {
            return "[error] feed";
        }
        det_.term();
        
        const char* charset = det_.getCharset();
        if (!std::strcmp(charset, "UTF-8") || !std::strcmp(charset, "ASCII")) {
            return in;
        }
        if (charset[0] == '\0') {
            charset = "GBK"; // XXX
        }

        iconvpp::converter conv("UTF-8", charset);
        std::string ret;
        conv.convert(std::move(in), ret);
        return ret;
    }

private:
    CharDetector det_;
};
