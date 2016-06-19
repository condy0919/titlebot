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
        if (!std::strcmp(charset, "UTF-8") ||
            !strncasecmp(charset, "ASCII", sizeof("ASCII") - 1)) {
            return in;
        }

        // unknown type. using `ASCII` charset anyway
        // see uchardet 0.0.1 for detail.
        if (charset[0] == '\0') {
            charset = "ASCII"; // XXX
        }

        iconvpp::converter conv("UTF-8", charset);
        std::string ret;
        conv.convert(std::move(in), ret);
        return ret;
    }

private:
    CharDetector det_;
};
