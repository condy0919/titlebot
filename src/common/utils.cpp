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
inline constexpr std::array<std::tuple<const char*, unsigned>, 256> entity_names = {{
    {"nbsp", 160},
    {"iexcl", 161},
    {"cent", 162},
    {"pound", 163},
    {"curren", 164},
    {"yen", 165},
    {"brvbar", 166},
    {"sect", 167},
    {"uml", 168},
    {"copy", 169},
    {"ordf", 170},
    {"laquo", 171},
    {"not", 172},
    {"shy", 173},
    {"reg", 174},
    {"macr", 175},
    {"deg", 176},
    {"plusmn", 177},
    {"sup2", 178},
    {"sup3", 179},
    {"acute", 180},
    {"micro", 181},
    {"para", 182},
    {"middot", 183},
    {"cedil", 184},
    {"sup1", 185},
    {"ordm", 186},
    {"raquo", 187},
    {"frac14", 188},
    {"frac12", 189},
    {"frac34", 190},
    {"iquest", 191},
    {"Agrave", 192},
    {"Aacute", 193},
    {"Acirc", 194},
    {"Atilde", 195},
    {"Auml", 196},
    {"Aring", 197},
    {"AElig", 198},
    {"Ccedil", 199},
    {"Egrave", 200},
    {"Eacute", 201},
    {"Ecirc", 202},
    {"Euml", 203},
    {"Igrave", 204},
    {"Iacute", 205},
    {"Icirc", 206},
    {"Iuml", 207},
    {"ETH", 208},
    {"Ntilde", 209},
    {"Ograve", 210},
    {"Oacute", 211},
    {"Ocirc", 212},
    {"Otilde", 213},
    {"Ouml", 214},
    {"times", 215},
    {"Oslash", 216},
    {"Ugrave", 217},
    {"Uacute", 218},
    {"Ucirc", 219},
    {"Uuml", 220},
    {"Yacute", 221},
    {"THORN", 222},
    {"szlig", 223},
    {"agrave", 224},
    {"aacute", 225},
    {"acirc", 226},
    {"atilde", 227},
    {"auml", 228},
    {"aring", 229},
    {"aelig", 230},
    {"ccedil", 231},
    {"egrave", 232},
    {"eacute", 233},
    {"ecirc", 234},
    {"euml", 235},
    {"igrave", 236},
    {"iacute", 237},
    {"icirc", 238},
    {"iuml", 239},
    {"eth", 240},
    {"ntilde", 241},
    {"ograve", 242},
    {"oacute", 243},
    {"ocirc", 244},
    {"otilde", 245},
    {"ouml", 246},
    {"divide", 247},
    {"oslash", 248},
    {"ugrave", 249},
    {"uacute", 250},
    {"ucirc", 251},
    {"uuml", 252},
    {"yacute", 253},
    {"thorn", 254},
    {"yuml", 255},
    {"fnof", 402},
    /* Greek */
    {"Alpha", 913},
    {"Beta", 914},
    {"Gamma", 915},
    {"Delta", 916},
    {"Epsilon", 917},
    {"Zeta", 918},
    {"Eta", 919},
    {"Theta", 920},
    {"Iota", 921},
    {"Kappa", 922},
    {"Lambda", 923},
    {"Mu", 924},
    {"Nu", 925},
    {"Xi", 926},
    {"Omicron", 927},
    {"Pi", 928},
    {"Rho", 929},
    {"Sigma", 931},
    {"Tau", 932},
    {"Upsilon", 933},
    {"Phi", 934},
    {"Chi", 935},
    {"Psi", 936},
    {"Omega", 937},
    {"alpha", 945},
    {"beta", 946},
    {"gamma", 947},
    {"delta", 948},
    {"epsilon", 949},
    {"zeta", 950},
    {"eta", 951},
    {"theta", 952},
    {"iota", 953},
    {"kappa", 954},
    {"lambda", 955},
    {"mu", 956},
    {"nu", 957},
    {"xi", 958},
    {"omicron", 959},
    {"pi", 960},
    {"rho", 961},
    {"sigmaf", 962},
    {"sigma", 963},
    {"tau", 964},
    {"upsilon", 965},
    {"phi", 966},
    {"chi", 967},
    {"psi", 968},
    {"omega", 969},
    {"thetasym", 977},
    {"upsih", 978},
    {"piv", 982},
    /* General Punctuation */
    {"bull", 8226},
    {"hellip", 8230},
    {"prime", 8242},
    {"Prime", 8243},
    {"oline", 8254},
    {"frasl", 8260},
    /* Letterlike Symbols */
    {"weierp", 8472},
    {"image", 8465},
    {"real", 8476},
    {"trade", 8482},
    {"alefsym", 8501},
    /* Arrows */
    {"larr", 8592},
    {"uarr", 8593},
    {"rarr", 8594},
    {"darr", 8595},
    {"harr", 8596},
    {"crarr", 8629},
    {"lArr", 8656},
    {"uArr", 8657},
    {"rArr", 8658},
    {"dArr", 8659},
    {"hArr", 8660},
    /* Mathematical Operators */
    {"forall", 8704},
    {"part", 8706},
    {"exist", 8707},
    {"empty", 8709},
    {"nabla", 8711},
    {"isin", 8712},
    {"notin", 8713},
    {"ni", 8715},
    {"prod", 8719},
    {"sum", 8721},
    {"minus", 8722},
    {"lowast", 8727},
    {"radic", 8730},
    {"prop", 8733},
    {"infin", 8734},
    {"and", 8743},
    {"or", 8744},
    {"cap", 8745},
    {"cup", 8746},
    {"int", 8747},
    {"there4", 8756},
    {"sim", 8764},
    {"cong", 8773},
    {"asymp", 8776},
    {"ne", 8800},
    {"equiv", 8801},
    {"le", 8804},
    {"ge", 8805},
    {"sub", 8834},
    {"sup", 8835},
    {"nsub", 8836},
    {"sube", 8838},
    {"supe", 8839},
    {"oplus", 8853},
    {"otimes", 8855},
    {"perp", 8869},
    {"sdot", 8901},
    /* Miscellaneous Technical */
    {"lceil", 8968},
    {"rceil", 8969},
    {"lfloor", 8970},
    {"rfloor", 8971},
    {"lang", 9001},
    {"rang", 9002},
    /* Geometric Shapes */
    {"loz", 9674},
    /* Miscellaneous Symbols */
    {"spades", 9824},
    {"clubs", 9827},
    {"hearts", 9829},
    {"diams", 9830},
    {"quot", 34},
    {"amp", 38},
    {"AMP", 38},
    {"LT", 60},
    {"GT", 62},
    {"QUOT", 34},
    {"apos", 39},
    {"lt", 60},
    {"gt", 62},
    /* Latin Extended-A */
    {"OElig", 338},
    {"oelig", 339},
    {"Scaron", 352},
    {"scaron", 353},
    {"Yuml", 376},
    /* Spacing Modifier Letters */
    {"circ", 710},
    {"tilde", 732},
    /* General Punctuation */
    {"ensp", 8194},
    {"emsp", 8195},
    {"thinsp", 8201},
    {"zwnj", 8204},
    {"zwj", 8205},
    {"lrm", 8206},
    {"rlm", 8207},
    {"ndash", 8211},
    {"mdash", 8212},
    {"lsquo", 8216},
    {"rsquo", 8217},
    {"sbquo", 8218},
    {"ldquo", 8220},
    {"rdquo", 8221},
    {"bdquo", 8222},
    {"dagger", 8224},
    {"Dagger", 8225},
    {"permil", 8240},
    {"lsaquo", 8249},
    {"rsaquo", 8250},
    {"euro", 8364},
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
            auto it = std::ranges::find_if(
                entity_names, [name](const auto& entity) { return std::get<0>(entity) == name; });
            if (it != entity_names.end()) {
                // A known name contributes its character to the output, e.g. "&lt;" becomes "<".
                out += encode_utf8_code_point(std::get<1>(*it));
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
        for (auto [label, backend] : {std::pair{"gb2312", "GB18030"}, {"gbk", "GB18030"},
                                      {"iso-8859-1", "WINDOWS-1252"}, {"latin1", "WINDOWS-1252"},
                                      {"shift_jis", "CP932"}, {"big5", "BIG5-HKSCS"},
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
