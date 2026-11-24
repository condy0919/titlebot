#include "utils.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <charconv>
#include <cerrno>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <tuple>

#include <iconv.h>
#include <uchardet/uchardet.h>

#include <boost/scope_exit.hpp>

namespace {
inline constexpr std::array<std::tuple<const char*, unsigned>, 252> entity_names = {{
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

// HTTP chars that need quoting.
inline constexpr bool is_tspecial(char c) {
    switch (c) {
    case '(':
    case ')':
    case '<':
    case '>':
    case '@':
    case ',':
    case ';':
    case ':':
    case '\\':
    case '\"':
    case '[':
    case ']':
    case '?':
    case '=':
    case '{':
    case '}':
    case ' ':
    case '\t':
        return true;

    default:
        return false;
    }
}

inline constexpr std::string toutf8(unsigned ch) {
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

inline constexpr int tohex(int c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    } else if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    } else {
        return c - 'A' + 10;
    }
}

std::optional<std::string> iconv(const std::string& s, const char* encodings[], std::size_t n) {
    // Copy the string to a buffer as `iconv` requires a non-const char pointer.
    std::string in_buf(s);
    std::string buf(2048, ' ');
    for (std::size_t i = 0; i < n; ++i) {
        if (*encodings[i] == '\0') {
            continue;
        }

        iconv_t conv = ::iconv_open("UTF-8", encodings[i]);
        if (conv == (iconv_t)-1) { // NOLINT
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
            if (ret == static_cast<std::size_t>(-1) && errno != E2BIG) {
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
std::optional<std::string> HttpResponse::get_header(const std::string& key, bool lower_case) const {
    for (const auto& [k, v] : headers_) {
        if (k == key) {
            std::string val = v;
            if (lower_case) {
                // std::tolower has an variant about std::locale
                std::transform(val.begin(), val.end(), val.begin(), ::tolower);
            }
            return val;
        }
    }

    return std::nullopt;
}

HttpResponse::Parser::Status HttpResponse::Parser::consume(HttpResponse& resp, char c) {
    switch (state_) {
    case http_version_h:
        if (c == 'H') {
            state_ = http_version_t_1;
            return Status::Indeterminate;
        } else {
            return Status::Bad;
        }

    case http_version_t_1:
        if (c == 'T') {
            state_ = http_version_t_2;
            return Status::Indeterminate;
        } else {
            return Status::Bad;
        }

    case http_version_t_2:
        if (c == 'T') {
            state_ = http_version_p;
            return Status::Indeterminate;
        } else {
            return Status::Bad;
        }

    case http_version_p:
        if (c == 'P') {
            state_ = http_version_slash;
            return Status::Indeterminate;
        } else {
            return Status::Bad;
        }

    case http_version_slash:
        if (c == '/') {
            resp.major_ = resp.minor_ = 0;
            state_ = http_version_major_start;
            return Status::Indeterminate;
        } else {
            return Status::Bad;
        }

    case http_version_major_start:
        if (std::isdigit(c)) {
            resp.major_ = resp.major_ * 10 + (c - '0');
            state_ = http_version_major;
            return Status::Indeterminate;
        } else {
            return Status::Bad;
        }

    case http_version_major:
        if (c == '.') {
            state_ = http_version_minor_start;
            return Status::Indeterminate;
        } else if (std::isdigit(c)) {
            resp.major_ = resp.major_ * 10 + (c - '0');
            return Status::Indeterminate;
        } else {
            return Status::Bad;
        }

    case http_version_minor_start:
        if (std::isdigit(c)) {
            resp.minor_ = resp.minor_ * 10 + (c - '0');
            state_ = http_version_minor;
            return Status::Indeterminate;
        } else {
            return Status::Bad;
        }

    case http_version_minor:
        if (c == ' ') {
            resp.status_code_ = 0;
            state_ = http_status_code;
            return Status::Indeterminate;
        } else if (std::isdigit(c)) {
            resp.minor_ = resp.minor_ * 10 + (c - '0');
            return Status::Indeterminate;
        } else {
            return Status::Bad;
        }

    case http_status_code:
        if (std::isdigit(c)) {
            resp.status_code_ = resp.status_code_ * 10 + (c - '0');
            return Status::Indeterminate;
        } else if (c == ' ') {
            state_ = http_status_msg;
            return Status::Indeterminate;
        } else {
            return Status::Bad;
        }

    case http_status_msg:
        if (c == '\r') {
            state_ = expecting_newline_1;
            return Status::Indeterminate;
        } else if (isascii(c)) {
            resp.desc_.push_back(c);
            return Status::Indeterminate;
        } else {
            return Status::Bad;
        }

    case expecting_newline_1:
        if (c == '\n') {
            state_ = header_line_start;
            return Status::Indeterminate;
        } else {
            return Status::Bad;
        }

    case header_line_start:
        if (c == '\r') {
            state_ = expecting_newline_3;
            return Status::Indeterminate;
        } else if (!resp.headers_.empty() && (c == ' ' || c == '\t')) {
            state_ = header_lws;
            return Status::Indeterminate;
        } else if (!isascii(c) || std::iscntrl(c) || is_tspecial(c)) {
            return Status::Bad;
        } else {
            resp.headers_.emplace_back();
            resp.headers_.back().first.push_back(static_cast<char>(std::tolower(c)));
            state_ = header_name;
            return Status::Indeterminate;
        }

    case header_lws:
        if (c == '\r') {
            state_ = expecting_newline_2;
            return Status::Indeterminate;
        } else if (c == ' ' || c == '\t') {
            return Status::Indeterminate;
        } else if (std::iscntrl(c)) {
            return Status::Bad;
        } else {
            state_ = header_value;
            resp.headers_.back().second.push_back(c);
            return Status::Indeterminate;
        }

    case header_name:
        if (c == ':') {
            state_ = space_before_header_value;
            return Status::Indeterminate;
        } else if (!isascii(c) || std::iscntrl(c) || is_tspecial(c)) {
            return Status::Bad;
        } else {
            resp.headers_.back().first.push_back(static_cast<char>(std::tolower(c)));
            return Status::Indeterminate;
        }

    case space_before_header_value:
        if (c == ' ') {
            return Status::Indeterminate;
        } else if (c == '\r') {
            state_ = expecting_newline_2;
            return Status::Indeterminate;
        } else if (isascii(c)) {
            resp.headers_.back().second.push_back(c);
            state_ = header_value;
            return Status::Indeterminate;
        } else {
            return Status::Bad;
        }

    case header_value:
        if (c == '\r') {
            state_ = expecting_newline_2;
            return Status::Indeterminate;
        } else if (std::iscntrl(c)) {
            return Status::Bad;
        } else {
            resp.headers_.back().second.push_back(c);
            return Status::Indeterminate;
        }

    case expecting_newline_2:
        if (c == '\n') {
            state_ = header_line_start;
            return Status::Indeterminate;
        } else {
            return Status::Bad;
        }

    case expecting_newline_3:
        return (c == '\n') ? Status::Good : Status::Bad;

    default:
        return Status::Bad;
    }
}

HttpChunk::Parser::Status HttpChunk::Parser::consume(HttpChunk& chunk, char c) {
    switch (state_) {
    case token_start:
        chunk.data_.clear();
        if (std::isxdigit(c)) {
            chunk.size_ = tohex(c);
            state_ = token_size;
            return Status::Indeterminate;
        } else {
            return Status::Bad;
        }

    case token_size:
        if (std::isxdigit(c)) {
            chunk.size_ = 16 * chunk.size_ + tohex(c);
            return Status::Indeterminate;
        } else if (c == '\r') {
            state_ = token_cr1;
            return Status::Indeterminate;
        } else {
            state_ = token_extension;
            return Status::Indeterminate;
        }

    case token_extension:
        if (c == '\r') {
            state_ = token_cr1;
            return Status::Indeterminate;
        } else {
            return Status::Indeterminate;
        }

    case token_cr1:
        if (c == '\n') {
            state_ = token_lf1;
            return Status::Indeterminate;
        } else {
            return Status::Bad;
        }

    case token_lf1:
        if (chunk.size_ == 0) {
            if (c == '\r') {
                state_ = token_cr3;
            } else {
                state_ = token_trailer;
            }
            return Status::Indeterminate;
        } else {
            chunk.data_.push_back(c);
            state_ = token_data;
            return Status::Indeterminate;
        }

    case token_data:
        if (chunk.data_.size() < chunk.size_) {
            chunk.data_.push_back(c);
            return Status::Indeterminate;
        } else if (chunk.data_.size() == chunk.size_ && c == '\r') {
            state_ = token_cr2;
            return Status::Indeterminate;
        } else {
            return Status::Bad;
        }

    case token_cr2:
        if (c == '\n') {
            state_ = token_start;
            return Status::Good;
        } else {
            return Status::Bad;
        }

    case token_trailer:
        if (c == '\r') {
            state_ = token_trailer_cr;
            return Status::Indeterminate;
        } else {
            return Status::Indeterminate;
        }

    case token_trailer_cr:
        if (c == '\n') {
            state_ = token_trailer_lf;
            return Status::Indeterminate;
        } else {
            return Status::Bad;
        }

    case token_trailer_lf:
        if (c == '\r') {
            state_ = token_cr3;
            return Status::Indeterminate;
        } else {
            state_ = token_trailer;
            return Status::Indeterminate;
        }

    case token_cr3:
        if (c == '\n') {
            return Status::Good;
        } else {
            return Status::Bad;
        }

    default:
        return Status::Bad;
    }
}

std::string unquote(const char* s) {
    std::string out;
    while (*s) {
        if (*s != '&') {
            out += *s++;
            continue;
        }
        const char* end = s + 1;
        while (std::isalnum(static_cast<unsigned char>(*end)) || *end == '#') {
            ++end;
        }
        if (*end != ';') {
            out += *s++;
            continue;
        }
        std::string_view name(s + 1, end);
        if (name.starts_with('#')) {
            name.remove_prefix(1);
            bool hex = name.starts_with('x') || name.starts_with('X');
            name.remove_prefix(hex ? 1 : 0);
            unsigned code = 0;
            auto parsed = std::from_chars(name.data(), end, code, hex ? 16 : 10);
            if (parsed.ec == std::errc{} && parsed.ptr == end && code && code <= 0x10ffff &&
                !(code >= 0xd800 && code <= 0xdfff)) {
                out += toutf8(code);
            } else {
                out.append(s, end + 1);
            }
        } else {
            auto it = std::find_if(entity_names.begin(), entity_names.end(),
                                   [name](const auto& entity) { return std::get<0>(entity) == name; });
            if (it != entity_names.end()) {
                out += toutf8(std::get<1>(*it));
            } else {
                out.append(s, end + 1);
            }
        }
        s = end + 1;
    }
    return out;
}

std::string make_request(const std::string& host, const std::string& uri) {
    std::string req;
    req.reserve(512);
    req.append("GET ").append(uri).append(" HTTP/1.1\r\n");
    req.append("Accept: */*\r\n");
    req.append("Accept-Encoding: gzip,deflate,identity\r\n");
    req.append("Accept-Language: zh-CN,zh;q=0.9,en;q=0.8,zh-TW;q=0.7,ja;q=0.6");
    req.append("Host: ").append(host).append("\r\n");
    req.append("User-Agent: titlebot\r\n");
    req.append("Connection: close\r\n\r\n");

    return req;
}

std::optional<std::string> utf8(const std::string& in, const std::string& charset) {
    const char* declared[] = {charset.c_str()};
    if (!charset.empty()) {
        return iconv(in, declared, 1);
    }
    const char* unicode[] = {"UTF-8"};
    if (auto valid = iconv(in, unicode, 1)) {
        return valid;
    }
    // 短标题的统计探测可能将繁体中文误判为单字节编码，优先沿用项目的东亚编码顺序。
    const char* encodings[] = {"GB18030", "Big5", "EUC-TW", "EUC-JP", "EUC-KR", "CP949", "SJIS"};
    if (auto text = iconv(in, encodings, std::size(encodings))) {
        return text;
    }
    uchardet_t ud = ::uchardet_new();
    if (!ud) {
        return std::nullopt;
    }
    BOOST_SCOPE_EXIT(&ud) {
        uchardet_delete(ud);
    }
    BOOST_SCOPE_EXIT_END
    if (::uchardet_handle_data(ud, in.data(), in.size()) != 0) {
        return std::nullopt;
    }
    ::uchardet_data_end(ud);
    const char* guessed[] = {::uchardet_get_charset(ud)};
    return iconv(in, guessed, 1);
}

std::tuple<std::string, std::string, std::string> parse_url(const std::string& url) {
    constexpr char delim[] = {':', '/', '/'};

    // Determine the schema
    std::string schema;
    auto colon_start = std::search(url.begin(), url.end(), std::begin(delim), std::end(delim));
    if (colon_start != url.end()) {
        schema.assign(url.begin(), colon_start);
        std::advance(colon_start, 3);
    } else {
        schema = "http";
        colon_start = url.begin();
    }

    // Determine the host and the uri
    std::string uri;
    std::string host(colon_start, url.end());
    if (auto iter = std::find(host.begin(), host.end(), '/'); iter != host.end()) {
        uri.assign(iter, host.end());
        host.erase(iter, host.end());
    } else {
        uri = "/";
    }

    // Remove the fragments.
    if (auto iter = std::find(uri.begin(), uri.end(), '#'); iter != uri.end()) {
        uri.erase(iter, uri.end());
    }

    return {std::move(schema), std::move(host), std::move(uri)};
}
} // namespace tb
