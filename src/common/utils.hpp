#ifndef TITLEBOT_UTILS_HPP_
#define TITLEBOT_UTILS_HPP_

#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace tb {
/// Decode named and numeric HTML character references, preserving unrecognized references.
/// For example, `decode_html_entities("template&lt;T&gt; foo")` returns `"template<T> foo"`.
std::string decode_html_entities(std::string_view text);

/// Convert in to UTF-8 using the supplied charset. If charset is empty, try UTF-8 first,
/// then use in and the optional sample to detect the encoding. Return std::nullopt on failure.
/// For example, `convert_to_utf8("Caf\xE9", "ISO-8859-1")` returns an optional containing `"Café"`.
std::optional<std::string> convert_to_utf8(const std::string& in, const std::string& charset = {},
                                           std::string_view sample = {});
enum class TitleFormatError {
    kDecode,
};

/// Decode and normalize a raw HTML title. For example, " A &amp; B " in UTF-8 yields "A & B".
std::expected<std::optional<std::string>, TitleFormatError>
FormatHtmlTitle(const std::string& title, const std::string& charset, bool bom,
                const std::string& meta, std::string_view sample);
} // namespace tb

#endif // TITLEBOT_UTILS_HPP_
