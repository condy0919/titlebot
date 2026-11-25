#ifndef IRCBOT_FEATURES_HPP_
#define IRCBOT_FEATURES_HPP_

#include <cstddef>
#include <map>
#include <string>
#include <string_view>

#include "message.hpp"

namespace ircbot {
enum class CaseMapping { kAscii, kRfc1459, kStrictRfc1459 };

/// For example, FoldCase('[', kRfc1459) returns '{', but kAscii leaves '[' unchanged.
char FoldCase(char ch, CaseMapping mapping);

/// For example, Equals("Bot[", "bot{", kRfc1459) is true; kAscii makes it false.
bool Equals(std::string_view left, std::string_view right, CaseMapping mapping);

struct ServerFeatures {
    static constexpr std::size_t kMaxStoredFeatures = 256;
    static constexpr std::size_t kMaxStoredBytes = 64 * 1024;

    CaseMapping casemapping = CaseMapping::kRfc1459;
    std::size_t nick_length = 9;
    std::string channel_types = "#&";
    std::string status_prefixes;
    std::map<std::string, std::string> values;

    /// Apply 005 tokens across messages, preserving unknown advertised values within the entry
    /// and key/value byte budgets above. Updates and recognized fields may evict unknown entries;
    /// new unknown fields are ignored when full. For example,
    /// ":s 005 bot CASEMAPPING=ascii NICKLEN=20 :supported" selects ASCII and a 20-byte nick limit.
    /// A later "-NICKLEN" token restores the default limit of 9.
    void Observe(const MessageView& message);
};
} // namespace ircbot

#endif // IRCBOT_FEATURES_HPP_
