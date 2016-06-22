#pragma once

#include "iconv.hpp"
#include "chardet.hpp"
#include <memory>
#include <cstring>

class UTF8Translator {
public:
    UTF8Translator() = default;

    std::string trans(std::string in) {
        const static char* probers[] = {"GB18030", "Big5",  "EUCTW", "EUCJP",
                                        "EUCKR",   "CP949", "SJIS",  "UTF-8"};
        const char** prober = probers;
        std::string ret;

        while (prober != std::end(probers)) {
            det_.reset();
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

            // unknown type.
            if (charset[0] == '\0') {
                charset = *prober++;
            }
            
            try {
                iconvpp::converter conv("UTF-8", charset);
                conv.convert(std::move(in), ret);
            } catch (const iconvpp::InvalidMultibyteError& err) {
                continue;
            } catch (const std::runtime_error& err) {
                return err.what();
            } catch (...) {
                return "unknown error";
            }
            return ret;
        }
        return "";
    }

private:
    CharDetector det_;
};
