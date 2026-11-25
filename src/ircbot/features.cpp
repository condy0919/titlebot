#include "ircbot/features.hpp"

#include <algorithm>
#include <charconv>
#include <system_error>
#include <utility>

namespace ircbot {
char FoldCase(char ch, CaseMapping mapping) {
    if (ch >= 'A' && ch <= 'Z') {
        return static_cast<char>(ch + ('a' - 'A'));
    }
    if (mapping != CaseMapping::kAscii) {
        switch (ch) {
        case '[':
            return '{';
        case ']':
            return '}';
        case '\\':
            return '|';
        case '^':
            return mapping == CaseMapping::kRfc1459 ? '~' : ch;
        }
    }
    return ch;
}

bool Equals(std::string_view left, std::string_view right, CaseMapping mapping) {
    auto fold = [mapping](char ch) { return FoldCase(ch, mapping); };
    return std::ranges::equal(left, right, {}, fold, fold);
}

namespace {
bool KnownFeature(std::string_view name) {
    return name == "CASEMAPPING" || name == "NICKLEN" || name == "CHANTYPES" || name == "STATUSMSG";
}

std::string DecodeFeatureValue(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && value.size() - i >= 4 && value[i + 1] == 'x') {
            const auto code = value.substr(i + 2, 2);
            if (code == "20" || code == "5C" || code == "5c" || code == "3D" || code == "3d") {
                result += code == "20" ? ' ' : (code == "5C" || code == "5c" ? '\\' : '=');
                i += 3;
                continue;
            }
        }
        result += value[i];
    }
    return result;
}
} // namespace

void ServerFeatures::Observe(const MessageView& message) {
    if (!IsCommand(message, "005") || message.params.size() < 3) {
        return;
    }
    // Compute from the public snapshot so callers cannot invalidate a hidden byte counter.
    std::size_t stored_bytes = 0;
    for (const auto& [name, value] : values) {
        stored_bytes += name.size() + value.size();
    }
    // The first parameter is the recipient; the final parameter is descriptive text.
    for (std::size_t i = 1; i + 1 < message.params.size(); ++i) {
        auto token = message.params[i];
        if (token.size() > 510) {
            continue;
        }
        const bool removed = token.starts_with('-');
        if (removed) {
            token.remove_prefix(1);
        }
        const auto equal = token.find('=');
        const auto name_view = token.substr(0, equal);
        if (name_view.empty() || name_view.size() > 20 || (removed && equal != token.npos) ||
            !std::ranges::all_of(name_view, [](char ch) {
                return (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '.' ||
                       ch == '/';
            })) {
            continue;
        }
        const std::string name(name_view);
        const auto previous = values.find(name);
        const auto previous_bytes =
            previous == values.end() ? 0 : previous->first.size() + previous->second.size();
        std::string_view value;
        if (removed) {
            if (previous == values.end()) {
                continue;
            }
            stored_bytes -= previous_bytes;
            values.erase(previous);
        } else {
            auto decoded =
                equal == token.npos ? std::string{} : DecodeFeatureValue(token.substr(equal + 1));
            const auto incoming_bytes = name.size() + decoded.size();
            const bool inserted = previous == values.end();
            auto fits = [&] {
                return values.size() + inserted <= kMaxStoredFeatures &&
                       stored_bytes - previous_bytes + incoming_bytes <= kMaxStoredBytes;
            };
            if (!fits()) {
                if (inserted && !KnownFeature(name)) {
                    continue;
                }
                // An unknown-token flood must not block CASEMAPPING updates or grow a previously
                // stored value beyond the budget. Preserve recognized fields when making room.
                while (!fits()) {
                    const auto victim = std::ranges::find_if(values, [&](const auto& item) {
                        return item.first != name && !KnownFeature(item.first);
                    });
                    if (victim == values.end()) {
                        break;
                    }
                    stored_bytes -= victim->first.size() + victim->second.size();
                    values.erase(victim);
                }
                if (!fits()) {
                    continue;
                }
            }
            const auto entry = values.insert_or_assign(name, std::move(decoded)).first;
            stored_bytes = stored_bytes - previous_bytes + incoming_bytes;
            value = entry->second;
        }
        if (name == "CASEMAPPING") {
            if (removed || value == "rfc1459") {
                casemapping = CaseMapping::kRfc1459;
            } else if (value == "ascii") {
                casemapping = CaseMapping::kAscii;
            } else if (value == "rfc1459-strict" || value == "strict-rfc1459") {
                casemapping = CaseMapping::kStrictRfc1459;
            }
        } else if (name == "NICKLEN") {
            if (removed) {
                nick_length = 9;
            } else {
                std::size_t limit = 0;
                const auto [end, error] =
                    std::from_chars(value.data(), value.data() + value.size(), limit);
                if (error == std::errc{} && end == value.data() + value.size() && limit) {
                    nick_length = limit;
                }
            }
        } else if (name == "CHANTYPES") {
            channel_types = removed ? "#&" : std::string(value);
        } else if (name == "STATUSMSG") {
            status_prefixes = removed ? "" : std::string(value);
        }
    }
}
} // namespace ircbot
