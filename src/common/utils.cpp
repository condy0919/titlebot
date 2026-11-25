#include "utils.hpp"

#include <iconv.h>
#include <uchardet/uchardet.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <memory>
#include <optional>
#include <string>
#include <tuple>

#include <boost/beast/core/string.hpp>
#include <boost/locale/utf.hpp>

namespace {
// Each entry maps an HTML entity name to its Unicode code point, written in decimal.
// For example, {"euro", 8364} maps &euro; to U+20AC, which the decoder encodes as UTF-8.
inline constexpr std::array<std::pair<std::string_view, unsigned>, 256> entity_names = {{
    /* Arrows */
    /* General Punctuation */
    /* General Punctuation */
    /* Geometric Shapes */
    /* Greek */
    /* Latin Extended-A */
    /* Letterlike Symbols */
    /* Mathematical Operators */
    /* Miscellaneous Symbols */
    /* Miscellaneous Technical */
    /* Spacing Modifier Letters */
    {"AElig", 198},   {"AMP", 38},      {"Aacute", 193},  {"Acirc", 194},    {"Agrave", 192},
    {"Alpha", 913},   {"Aring", 197},   {"Atilde", 195},  {"Auml", 196},     {"Beta", 914},
    {"Ccedil", 199},  {"Chi", 935},     {"Dagger", 8225}, {"Delta", 916},    {"ETH", 208},
    {"Eacute", 201},  {"Ecirc", 202},   {"Egrave", 200},  {"Epsilon", 917},  {"Eta", 919},
    {"Euml", 203},    {"GT", 62},       {"Gamma", 915},   {"Iacute", 205},   {"Icirc", 206},
    {"Igrave", 204},  {"Iota", 921},    {"Iuml", 207},    {"Kappa", 922},    {"LT", 60},
    {"Lambda", 923},  {"Mu", 924},      {"Ntilde", 209},  {"Nu", 925},       {"OElig", 338},
    {"Oacute", 211},  {"Ocirc", 212},   {"Ograve", 210},  {"Omega", 937},    {"Omicron", 927},
    {"Oslash", 216},  {"Otilde", 213},  {"Ouml", 214},    {"Phi", 934},      {"Pi", 928},
    {"Prime", 8243},  {"Psi", 936},     {"QUOT", 34},     {"Rho", 929},      {"Scaron", 352},
    {"Sigma", 931},   {"THORN", 222},   {"Tau", 932},     {"Theta", 920},    {"Uacute", 218},
    {"Ucirc", 219},   {"Ugrave", 217},  {"Upsilon", 933}, {"Uuml", 220},     {"Xi", 926},
    {"Yacute", 221},  {"Yuml", 376},    {"Zeta", 918},    {"aacute", 225},   {"acirc", 226},
    {"acute", 180},   {"aelig", 230},   {"agrave", 224},  {"alefsym", 8501}, {"alpha", 945},
    {"amp", 38},      {"and", 8743},    {"apos", 39},     {"aring", 229},    {"asymp", 8776},
    {"atilde", 227},  {"auml", 228},    {"bdquo", 8222},  {"beta", 946},     {"brvbar", 166},
    {"bull", 8226},   {"cap", 8745},    {"ccedil", 231},  {"cedil", 184},    {"cent", 162},
    {"chi", 967},     {"circ", 710},    {"clubs", 9827},  {"cong", 8773},    {"copy", 169},
    {"crarr", 8629},  {"cup", 8746},    {"curren", 164},  {"dArr", 8659},    {"dagger", 8224},
    {"darr", 8595},   {"deg", 176},     {"delta", 948},   {"diams", 9830},   {"divide", 247},
    {"eacute", 233},  {"ecirc", 234},   {"egrave", 232},  {"empty", 8709},   {"emsp", 8195},
    {"ensp", 8194},   {"epsilon", 949}, {"equiv", 8801},  {"eta", 951},      {"eth", 240},
    {"euml", 235},    {"euro", 8364},   {"exist", 8707},  {"fnof", 402},     {"forall", 8704},
    {"frac12", 189},  {"frac14", 188},  {"frac34", 190},  {"frasl", 8260},   {"gamma", 947},
    {"ge", 8805},     {"gt", 62},       {"hArr", 8660},   {"harr", 8596},    {"hearts", 9829},
    {"hellip", 8230}, {"iacute", 237},  {"icirc", 238},   {"iexcl", 161},    {"igrave", 236},
    {"image", 8465},  {"infin", 8734},  {"int", 8747},    {"iota", 953},     {"iquest", 191},
    {"isin", 8712},   {"iuml", 239},    {"kappa", 954},   {"lArr", 8656},    {"lambda", 955},
    {"lang", 9001},   {"laquo", 171},   {"larr", 8592},   {"lceil", 8968},   {"ldquo", 8220},
    {"le", 8804},     {"lfloor", 8970}, {"lowast", 8727}, {"loz", 9674},     {"lrm", 8206},
    {"lsaquo", 8249}, {"lsquo", 8216},  {"lt", 60},       {"macr", 175},     {"mdash", 8212},
    {"micro", 181},   {"middot", 183},  {"minus", 8722},  {"mu", 956},       {"nabla", 8711},
    {"nbsp", 160},    {"ndash", 8211},  {"ne", 8800},     {"ni", 8715},      {"not", 172},
    {"notin", 8713},  {"nsub", 8836},   {"ntilde", 241},  {"nu", 957},       {"oacute", 243},
    {"ocirc", 244},   {"oelig", 339},   {"ograve", 242},  {"oline", 8254},   {"omega", 969},
    {"omicron", 959}, {"oplus", 8853},  {"or", 8744},     {"ordf", 170},     {"ordm", 186},
    {"oslash", 248},  {"otilde", 245},  {"otimes", 8855}, {"ouml", 246},     {"para", 182},
    {"part", 8706},   {"permil", 8240}, {"perp", 8869},   {"phi", 966},      {"pi", 960},
    {"piv", 982},     {"plusmn", 177},  {"pound", 163},   {"prime", 8242},   {"prod", 8719},
    {"prop", 8733},   {"psi", 968},     {"quot", 34},     {"rArr", 8658},    {"radic", 8730},
    {"rang", 9002},   {"raquo", 187},   {"rarr", 8594},   {"rceil", 8969},   {"rdquo", 8221},
    {"real", 8476},   {"reg", 174},     {"rfloor", 8971}, {"rho", 961},      {"rlm", 8207},
    {"rsaquo", 8250}, {"rsquo", 8217},  {"sbquo", 8218},  {"scaron", 353},   {"sdot", 8901},
    {"sect", 167},    {"shy", 173},     {"sigma", 963},   {"sigmaf", 962},   {"sim", 8764},
    {"spades", 9824}, {"sub", 8834},    {"sube", 8838},   {"sum", 8721},     {"sup", 8835},
    {"sup1", 185},    {"sup2", 178},    {"sup3", 179},    {"supe", 8839},    {"szlig", 223},
    {"tau", 964},     {"there4", 8756}, {"theta", 952},   {"thetasym", 977}, {"thinsp", 8201},
    {"thorn", 254},   {"tilde", 732},   {"times", 215},   {"trade", 8482},   {"uArr", 8657},
    {"uacute", 250},  {"uarr", 8593},   {"ucirc", 251},   {"ugrave", 249},   {"uml", 168},
    {"upsih", 978},   {"upsilon", 965}, {"uuml", 252},    {"weierp", 8472},  {"xi", 958},
    {"yacute", 253},  {"yen", 165},     {"yuml", 255},    {"zeta", 950},     {"zwj", 8205},
    {"zwnj", 8204},
}};

constexpr std::string encode_utf8_code_point(unsigned ch) {
    if (ch >= 0x10000) {
        return {static_cast<char>(0xf0 | (ch >> 18)), static_cast<char>(0x80 | ((ch >> 12) & 63)),
                static_cast<char>(0x80 | ((ch >> 6) & 63)), static_cast<char>(0x80 | (ch & 63))};
    }
    char buf[4];
    if (ch >= 0x800) {
        buf[0] = static_cast<char>(0xe0 | ((ch >> 12) & 0x0f));
        buf[1] = static_cast<char>(0x80 | ((ch >> 6) & 0x3f));
        buf[2] = static_cast<char>(0x80 | (ch & 0x3f));
        buf[3] = '\0';
    } else if (ch >= 0x80) {
        buf[0] = static_cast<char>(0xc0 | ((ch >> 6) & 0x1f));
        buf[1] = static_cast<char>(0x80 | (ch & 0x3f));
        buf[2] = '\0';
    } else {
        buf[0] = static_cast<char>(ch);
        buf[1] = '\0';
    }
    return buf;
}

bool ValidUtf8(std::string_view text) {
    auto it = text.begin();
    while (it != text.end()) {
        using namespace boost::locale::utf;
        auto cp = utf_traits<char>::decode(it, text.end());
        if (cp == illegal || cp == incomplete) {
            return false;
        }
    }
    return true;
}

std::optional<std::string> try_source_encodings(const std::string& s, const char* encodings[],
                                                std::size_t n) {
    // Copy the string to a buffer as `iconv` requires a non-const char pointer.
    std::string in_buf(s);
    std::array<char, 2048> buf;
    for (std::size_t i = 0; i < n; ++i) {
        if (*encodings[i] == '\0') {
            continue;
        }

        iconv_t conv = ::iconv_open("UTF-8", encodings[i]);
        if (conv == reinterpret_cast<iconv_t>(-1)) { // NOLINT
            continue;
        }

        std::string out;
        char* src_ptr = &in_buf[0];
        std::size_t src_sz = in_buf.size();
        while (src_sz) {
            char* dst_ptr = &buf[0];
            std::size_t dst_sz = buf.size();
            std::size_t ret = ::iconv(conv, &src_ptr, &src_sz, &dst_ptr, &dst_sz);
            out.append(buf.data(), buf.size() - dst_sz);
            // E2BIG means the next character needs more output space. Resume at src_ptr
            // with a fresh buffer. Other errors rule out this candidate encoding.
            if (ret == static_cast<std::size_t>(-1) && errno != E2BIG) { // NOLINT
                break;
            }
        }
        ::iconv_close(conv);
        if (src_sz == 0) {
            return out;
        }
    }

    return std::nullopt;
}
} // namespace

namespace tb {
std::string decode_html_entities(std::string_view text) {
    if (text.empty()) {
        return {};
    }

    auto s = text.data();
    const auto limit = s + text.size();
    std::string out;
    while (s != limit) {
        // Only '&' can start a reference. Copy every other byte verbatim, including UTF-8 bytes.
        if (*s != '&') {
            out += *s++;
            continue;
        }

        const char* end = s + 1;
        // Scan the candidate once: letters and digits cover names and hex values, and '#'
        // can introduce a numeric reference. Check the bound before inspecting the next byte.
        while (end != limit && (std::isalnum(static_cast<unsigned char>(*end)) || *end == '#')) {
            ++end;
        }

        // This decoder requires a semicolon. Consume only the '&' on a false start so that
        // a later reference still gets a chance to match: "&bad&amp;" becomes "&bad&".
        if (end == limit || *end != ';') {
            out += *s++;
            continue;
        }

        std::string_view name(s + 1, end);
        // A leading '#' selects numeric parsing; the remaining candidates use the name table.
        if (name.starts_with('#')) {
            name.remove_prefix(1);
            // Strip an optional 'x' or 'X' and select the matching base so that both
            // "&#65;" and "&#x41;" decode to "A".
            const bool hex = name.starts_with('x') || name.starts_with('X');
            name.remove_prefix(hex ? 1 : 0);

            unsigned code = 0;
            auto [ptr, ec] = std::from_chars(name.data(), end, code, hex ? 16 : 10); // NOLINT
            // Decode only if all five checks pass. Otherwise, keep the reference as written.
            // - ec: from_chars found digits and the value fits in unsigned.
            // - ptr == end: every character before ';' was consumed. For "&#12z;",
            //   from_chars succeeds with 12 but leaves ptr at 'z'.
            // - code != 0: preserve "&#0;" to avoid inserting a NUL byte into the title.
            // - code <= 0x10ffff: U+10FFFF is the highest Unicode code point.
            // - Outside U+D800..U+DFFF: UTF-16 surrogates cannot be encoded as UTF-8.
            if (ec == std::errc{} && ptr == end && code != 0 && code <= 0x10ffff &&
                !(code >= 0xd800 && code <= 0xdfff)) {
                // HTML uses legacy Windows-1252 mappings for numeric references in this
                // range. For example, &#128; represents the euro sign (U+20AC). Outside
                // 128-159, encode the parsed code point directly.
                constexpr unsigned replacements[] = {
                    0x20ac, 129,    0x201a, 0x192,  0x201e, 0x2026, 0x2020, 0x2021,
                    0x2c6,  0x2030, 0x160,  0x2039, 0x152,  141,    0x17d,  143,
                    144,    0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
                    0x2dc,  0x2122, 0x161,  0x203a, 0x153,  157,    0x17e,  0x178};
                out += encode_utf8_code_point(code >= 128 && code <= 159 ? replacements[code - 128]
                                                                         : code);
            } else {
                // Preserve the entire invalid reference, including '&' and ';'.
                out.append(s, end + 1);
            }
        } else {
            // Look up the exact name, including its case, to get the Unicode code point.
            auto it = std::ranges::lower_bound(entity_names, name, {},
                                               &std::pair<std::string_view, unsigned>::first);
            if (it != entity_names.end() && it->first == name) {
                // A known name contributes its character to the output, e.g. "&lt;" becomes "<".
                out += encode_utf8_code_point(it->second);
            } else {
                // An unknown name such as "&example;" passes through with its original spelling.
                out.append(s, end + 1);
            }
        }

        // Both branches consumed the semicolon. Resume in the input so decoded text is
        // never scanned a second time: "&amp;lt;" becomes "&lt;" in one call.
        s = end + 1;
    }
    return out;
}

std::optional<std::string> convert_to_utf8(const std::string& in, // NOLINT
                                           const std::string& charset, std::string_view sample) {
    if (!charset.empty()) {
        if (boost::beast::iequals(charset, "utf-8") || boost::beast::iequals(charset, "utf8")) {
            return ValidUtf8(in) ? std::optional(in) : std::nullopt;
        }
        const char* encoding = charset.c_str();
        for (auto [label, backend] : {std::pair{"gb2312", "GB18030"},
                                      {"gbk", "GB18030"},
                                      {"iso-8859-1", "WINDOWS-1252"},
                                      {"latin1", "WINDOWS-1252"},
                                      {"shift_jis", "CP932"},
                                      {"big5", "BIG5-HKSCS"},
                                      {"euc-kr", "CP949"}}) {
            if (boost::beast::iequals(charset, label)) {
                encoding = backend;
                break;
            }
        }
        const char* declared[] = {encoding};
        return try_source_encodings(in, declared, 1);
    }

    if (ValidUtf8(in)) {
        return in;
    }

    std::unique_ptr<uchardet, decltype(&::uchardet_delete)> detector(::uchardet_new(),
                                                                     &::uchardet_delete);
    if (!detector) {
        return std::nullopt;
    }

    if (sample.empty()) {
        sample = in;
    }

    // An ASCII-only page prefix gives few clues about the title's encoding. Include the
    // title bytes in the detector's sample if the prefix does not already contain them.
    if (::uchardet_handle_data(detector.get(), sample.data(), sample.size()) != 0 ||
        (sample.find(in) == sample.npos &&
         ::uchardet_handle_data(detector.get(), in.data(), in.size()) != 0)) {
        return std::nullopt;
    }
    ::uchardet_data_end(detector.get());

    const char* guessed[] = {::uchardet_get_charset(detector.get())};
    // Prefer legacy Chinese encodings when a short sample also fits the single-byte guess.
    const char* fallback[] = {"GB18030", "Big5", "EUC-JP", "EUC-KR", "SJIS"};
    std::string_view guess(guessed[0]);
    auto non_ascii = [](unsigned char c) { return c >= 128; };
    auto evidence = std::ranges::count_if(sample, non_ascii);
    if (sample.find(in) == sample.npos) {
        evidence += std::ranges::count_if(in, non_ascii);
    }
    if (evidence < 64 && (guess.starts_with("WINDOWS-") || guess.starts_with("ISO-8859"))) {
        if (auto text = try_source_encodings(in, fallback, std::size(fallback))) {
            return text;
        }
    }
    if (auto text = try_source_encodings(in, guessed, 1)) {
        return text;
    }
    return try_source_encodings(in, fallback, std::size(fallback));
}

} // namespace tb
