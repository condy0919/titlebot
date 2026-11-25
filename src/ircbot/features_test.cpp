#include "ircbot/features.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

namespace {
void Observe(ircbot::ServerFeatures& features, std::string_view line) {
    auto message = ircbot::ParseMessage(line);
    ASSERT_TRUE(message);
    features.Observe(*message);
}

TEST(IrcFeatures, DistinguishesAsciiStrictAndRfc1459Names) {
    using ircbot::CaseMapping;
    EXPECT_TRUE(ircbot::Equals("BoT", "bot", CaseMapping::kAscii));
    EXPECT_FALSE(ircbot::Equals("bot[", "bot{", CaseMapping::kAscii));
    EXPECT_TRUE(ircbot::Equals("bot[", "bot{", CaseMapping::kStrictRfc1459));
    EXPECT_TRUE(ircbot::Equals("[A]\\", "{a}|", CaseMapping::kRfc1459));
    EXPECT_FALSE(ircbot::Equals("bot^", "bot~", CaseMapping::kStrictRfc1459));
    EXPECT_TRUE(ircbot::Equals("bot^", "bot~", CaseMapping::kRfc1459));
    EXPECT_FALSE(ircbot::Equals("bot", "bots", CaseMapping::kRfc1459));
    EXPECT_EQ(ircbot::FoldCase('\xff', CaseMapping::kAscii), '\xff');
}

TEST(IrcFeatures, AccumulatesTokensAndAppliesAdvertisedCaseMapping) {
    ircbot::ServerFeatures features;
    Observe(features, ":irc 005 bot[ CASEMAPPING=ascii NICKLEN=20 :supported");
    EXPECT_EQ(features.casemapping, ircbot::CaseMapping::kAscii);
    EXPECT_EQ(features.nick_length, 20u);
    EXPECT_FALSE(ircbot::Equals("bot[", "bot{", features.casemapping));
    Observe(features, ":irc 005 bot[ CHANTYPES=# STATUSMSG=@+ NETWORK=Example :supported");
    EXPECT_EQ(features.channel_types, "#");
    EXPECT_EQ(features.status_prefixes, "@+");
    EXPECT_EQ(features.values.at("NETWORK"), "Example");
    EXPECT_EQ(features.nick_length, 20u);
    Observe(features, ":irc 005 bot[ CASEMAPPING=rfc1459-strict :supported");
    EXPECT_EQ(features.casemapping, ircbot::CaseMapping::kStrictRfc1459);
}

TEST(IrcFeatures, RemovalsRestoreDefaultsAndUnadvertiseValues) {
    ircbot::ServerFeatures features;
    Observe(features,
            ":s 005 bot CASEMAPPING=ascii NICKLEN=20 CHANTYPES=! STATUSMSG=@ FOO :supported");
    Observe(features, ":s 005 bot -CASEMAPPING -NICKLEN -CHANTYPES -STATUSMSG -FOO :supported");
    EXPECT_EQ(features.casemapping, ircbot::CaseMapping::kRfc1459);
    EXPECT_EQ(features.nick_length, 9u);
    EXPECT_EQ(features.channel_types, "#&");
    EXPECT_TRUE(features.status_prefixes.empty());
    EXPECT_TRUE(features.values.empty());
    Observe(features, ":s 005 bot -UNKNOWN :supported");
    EXPECT_TRUE(features.values.empty());
}

TEST(IrcFeatures, HandlesEmptyValuesEscapesAndUnknownTokens) {
    ircbot::ServerFeatures features;
    Observe(
        features,
        R"(:s 005 bot CHANTYPES STATUSMSG= NETWORK=one\x20two\x5C\x3D FUTURE=a\x00b :supported)");
    EXPECT_TRUE(features.channel_types.empty());
    EXPECT_TRUE(features.status_prefixes.empty());
    EXPECT_EQ(features.values.at("NETWORK"), "one two\\=");
    EXPECT_EQ(features.values.at("FUTURE"), R"(a\x00b)");
    Observe(features, ":s 005 bot NETWORK=changed :NICKLEN=1");
    EXPECT_EQ(features.values.at("NETWORK"), "changed");
    EXPECT_EQ(features.nick_length, 9u);
}

TEST(IrcFeatures, IgnoresInvalidKnownValuesAndForeignNumerics) {
    ircbot::ServerFeatures features;
    Observe(features, ":s 005 bot NICKLEN=20 CASEMAPPING=ascii :supported");
    for (auto line : {":s 005 bot NICKLEN=0 :supported", ":s 005 bot NICKLEN=-1 :supported",
                      ":s 005 bot NICKLEN=12junk :supported", ":s 005 bot NICKLEN :supported",
                      ":s 005 bot NICKLEN=999999999999999999999999 :supported",
                      ":s 105 bot NICKLEN=1 CASEMAPPING=rfc1459 :supported",
                      ":s 005 bot -NICKLEN=1 CASEMAPPING=future :supported", ":s 005 bot"}) {
        Observe(features, line);
        EXPECT_EQ(features.nick_length, 20u) << line;
        EXPECT_EQ(features.casemapping, ircbot::CaseMapping::kAscii) << line;
    }
}

TEST(IrcFeatures, BoundsStoredEntriesWithoutBlockingKnownFieldsOrUpdates) {
    ircbot::ServerFeatures features;
    for (unsigned i = 0; i < 1000; ++i) {
        Observe(features, ":s 005 bot X" + std::to_string(i) + "=old :supported");
    }
    ASSERT_EQ(features.values.size(), 256u);
    EXPECT_FALSE(features.values.contains("X999"));
    Observe(features, ":s 005 bot X255=new :supported");
    EXPECT_EQ(features.values.at("X255"), "new");
    Observe(features, ":s 005 bot CASEMAPPING=ascii NICKLEN=24 CHANTYPES=! STATUSMSG=@ :supported");
    EXPECT_EQ(features.casemapping, ircbot::CaseMapping::kAscii);
    EXPECT_EQ(features.nick_length, 24u);
    EXPECT_EQ(features.channel_types, "!");
    EXPECT_EQ(features.status_prefixes, "@");
    EXPECT_EQ(features.values.at("CASEMAPPING"), "ascii");
    EXPECT_EQ(features.values.size(), 256u);
    Observe(features, ":s 005 bot -CASEMAPPING -NICKLEN -CHANTYPES -STATUSMSG -X255 :supported");
    EXPECT_EQ(features.casemapping, ircbot::CaseMapping::kRfc1459);
    EXPECT_EQ(features.nick_length, 9u);
    EXPECT_EQ(features.channel_types, "#&");
    EXPECT_TRUE(features.status_prefixes.empty());
    EXPECT_FALSE(features.values.contains("X255"));
    Observe(features, ":s 005 bot NEW=value :supported");
    EXPECT_EQ(features.values.at("NEW"), "value");
}

TEST(IrcFeatures, BoundsStoredBytesAcrossGrowthRemovalAndRefill) {
    ircbot::ServerFeatures features;
    auto bytes = [&] {
        std::size_t total = 0;
        for (const auto& [name, value] : features.values) {
            total += name.size() + value.size();
        }
        return total;
    };
    for (unsigned i = 0; i < 1000; ++i) {
        Observe(features,
                ":s 005 bot X" + std::to_string(i) + "=" + std::string(400, 'x') + " :supported");
    }
    ASSERT_LT(features.values.size(), 256u);
    EXPECT_LE(bytes(), 64u * 1024);
    EXPECT_FALSE(features.values.contains("X999"));
    // Existing entries may grow, but must still honor the cumulative byte budget.
    for (unsigned i = 0; i < 100; ++i) {
        Observe(features,
                ":s 005 bot X" + std::to_string(i) + "=" + std::string(470, 'y') + " :supported");
        EXPECT_LE(bytes(), 64u * 1024);
    }
    EXPECT_EQ(features.values.at("X99"), std::string(470, 'y'));
    Observe(features, ":s 005 bot CASEMAPPING=ascii NICKLEN=32 :supported");
    EXPECT_EQ(features.casemapping, ircbot::CaseMapping::kAscii);
    EXPECT_EQ(features.nick_length, 32u);
    EXPECT_LE(bytes(), 64u * 1024);
    const auto before = bytes();
    Observe(features, ":s 005 bot -X99 :supported");
    EXPECT_EQ(bytes(), before - 473);
    Observe(features, ":s 005 bot NEW=" + std::string(400, 'z') + " :supported");
    EXPECT_EQ(features.values.at("NEW"), std::string(400, 'z'));
    EXPECT_LE(bytes(), 64u * 1024);
}
} // namespace
