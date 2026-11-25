#include "message.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>

namespace {
using ircbot::ParseMessage;
using ircbot::SerializeMessage;

TEST(IrcMessage, ReservesRelayPrefixBeforeComputingTextBudget) {
    EXPECT_EQ(ircbot::PrivmsgTextBudget("#r", 8), 488u);
    EXPECT_EQ(ircbot::PrivmsgTextBudget("#r", std::string_view::npos), 0u);
}

TEST(IrcMessage, SplitsAtUtf8Boundaries) {
    const auto parts = ircbot::SplitText("a猫b", 3);
    ASSERT_TRUE(parts);
    EXPECT_EQ(*parts, (std::vector<std::string_view>{"a", "猫", "b"}));
    EXPECT_FALSE(ircbot::SplitText("猫", 2));
    EXPECT_FALSE(ircbot::SplitText("a", 0));
}

TEST(MessagePrefix, ExtractsUserAndHost) {
    auto message = ircbot::ParseMessage(":nick!alice@host NOTICE bot :hello");
    ASSERT_TRUE(message);
    ASSERT_TRUE(ircbot::AsNotice(*message));
    EXPECT_EQ(ircbot::AsNotice(*message)->text, "hello");
    EXPECT_FALSE(ircbot::AsPrivmsg(*message));
    EXPECT_EQ(message->User(), "alice");
    EXPECT_EQ(message->Host(), "host");
    message = ircbot::ParseMessage(":nick@host NOTICE bot :hello");
    ASSERT_TRUE(message);
    EXPECT_TRUE(message->User().empty());
    EXPECT_EQ(message->Host(), "host");
}

TEST(IrcMessage, ParsesTagsPrefixAndParametersAsViews) {
    constexpr std::string_view line =
        "@label=hello\\sworld :alice!u@h pRiVmSg  #room  :hello : world ";
    const auto result = ParseMessage(line);
    ASSERT_TRUE(result);
    EXPECT_EQ(result->tags, "label=hello\\sworld");
    EXPECT_EQ(result->prefix, "alice!u@h");
    EXPECT_EQ(result->Nick(), "alice");
    EXPECT_TRUE(ircbot::IsCommand(*result, "PRIVMSG"));
    ASSERT_EQ(result->params.size(), 2u);
    EXPECT_EQ(result->params[0], "#room");
    EXPECT_EQ(result->params[1], "hello : world ");
    EXPECT_EQ(result->params[1].data(), line.data() + line.find("hello : world"));
    EXPECT_EQ(result->Tag("label"), "hello world");
}

TEST(IrcMessage, AcceptsOptionalSourcesAndEmptyFinalParameters) {
    for (auto line : {"PING :token", ":server PING :token", "@x=y  PING :token"}) {
        auto result = ParseMessage(line);
        ASSERT_TRUE(result) << line;
        ASSERT_EQ(result->params.size(), 1u);
        EXPECT_EQ(result->params.front(), "token");
    }
    auto result = ParseMessage("CAP * LS :");
    ASSERT_TRUE(result);
    ASSERT_EQ(result->params.size(), 3u);
    EXPECT_TRUE(result->params.back().empty());
    EXPECT_TRUE(result->Nick().empty());
    result = ParseMessage(":alice@host NICK bob");
    ASSERT_TRUE(result);
    EXPECT_EQ(result->Nick(), "alice");
    EXPECT_TRUE(ParseMessage("QUIT"));
    EXPECT_TRUE(ParseMessage(":s 001 bot :Welcome"));
}

TEST(IrcMessage, RejectsIncompleteOrInjectedLines) {
    for (auto line :
         {"", ":", ":server", ":server ", ": PING token", "@", "@x=y", "@x=y ", "@ PING token",
          " PING token", "12 x", "1234 x", "P1NG x", "PING\r\n", "PING :x\ny", "PING :x\ry"}) {
        EXPECT_FALSE(ParseMessage(line)) << line;
    }
    EXPECT_FALSE(ParseMessage(std::string_view("PING :x\0y", 9)));
    // Unknown alphabetic verbs are syntactically valid, even if they resemble truncated commands.
    EXPECT_TRUE(ParseMessage("PRIVM"));
}

TEST(IrcMessage, SupportsMoreThanFifteenIncomingParameters) {
    std::string line = "TEST";
    for (unsigned i = 0; i < 30; ++i) {
        line += " p" + std::to_string(i);
    }
    auto result = ParseMessage(line);
    ASSERT_TRUE(result);
    ASSERT_EQ(result->params.size(), 30u);
    EXPECT_EQ(result->params.back(), "p29");
}

TEST(IrcMessage, SeparatelyBoundsIncomingTagsAndBody) {
    const std::string body = "NOTICE :" + std::string(502, 'x');
    EXPECT_TRUE(ParseMessage(body));
    EXPECT_FALSE(ParseMessage(body + 'x'));
    const std::string tags = "@a=" + std::string(8187, 'x') + ' ';
    ASSERT_EQ(tags.size(), 8191u);
    EXPECT_TRUE(ParseMessage(tags + body));
    EXPECT_FALSE(ParseMessage(tags + body + 'x'));
    EXPECT_FALSE(ParseMessage("@a=" + std::string(8188, 'x') + " PING"));
}

TEST(IrcMessage, DecodesLastTagAndPreservesOpaqueKeys) {
    auto result = ParseMessage("@x=first;x=a\\:b\\sc\\\\d\\re\\nf\\q\\;empty;blank=;X=upper;"
                               "future/key/name=v PING token");
    ASSERT_TRUE(result);
    EXPECT_EQ(result->Tag("x"), "a;b c\\d\re\nfq");
    EXPECT_EQ(result->Tag("X"), "upper");
    EXPECT_EQ(result->Tag("empty"), "");
    EXPECT_EQ(result->Tag("blank"), "");
    EXPECT_EQ(result->Tag("future/key/name"), "v");
    EXPECT_FALSE(result->Tag("missing"));
}

TEST(IrcMessage, DropsInvalidUtf8TagValuesWithoutDroppingTheMessage) {
    for (const std::string bytes : {"\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe4\xb8"}) {
        const auto line = "@x=" + bytes + " PING token";
        auto result = ParseMessage(line);
        ASSERT_TRUE(result);
        EXPECT_FALSE(result->Tag("x"));
    }
    auto result = ParseMessage("@x=中文😀 PING token");
    ASSERT_TRUE(result);
    EXPECT_EQ(result->Tag("x"), "中文😀");
}

TEST(IrcMessage, SerializesTrailingParametersAndEscapedTags) {
    const std::array<std::string_view, 2> params{"#r", "hello world"};
    const std::array<ircbot::Tag, 2> tags{{{"+example.org/label", "a;b c\\d\r\n"}, {"flag", ""}}};
    auto line = SerializeMessage("PRIVMSG", params, tags);
    ASSERT_TRUE(line);
    EXPECT_EQ(*line, "@+example.org/label=a\\:b\\sc\\\\d\\r\\n;flag PRIVMSG #r :hello world");
    auto parsed = ParseMessage(*line);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->Tag(tags[0].key), tags[0].value);
    EXPECT_EQ(parsed->params[1], params[1]);
    for (std::string_view text : {"", ":token", "with spaces", "no-spaces"}) {
        const std::array args{text};
        line = SerializeMessage("CMD", args);
        ASSERT_TRUE(line);
        parsed = ParseMessage(*line);
        ASSERT_TRUE(parsed);
        ASSERT_EQ(parsed->params.size(), 1u);
        EXPECT_EQ(parsed->params[0], text);
    }
}

TEST(IrcMessage, RejectsOutgoingInjectionAndInvalidFields) {
    for (std::string_view command : {"", "CMD x", "CMD\n", "@CMD", "12", "CM1"}) {
        EXPECT_FALSE(SerializeMessage(command, {}));
    }
    for (std::string_view param : std::array<std::string_view, 3>{"line\rbreak", "line\nbreak",
                                                                  std::string_view("x\0y", 3)}) {
        const std::array params{param};
        EXPECT_FALSE(SerializeMessage("CMD", params));
    }
    for (std::string_view param : {"", "has space", ":leading"}) {
        const std::array params{param, std::string_view("last")};
        EXPECT_FALSE(SerializeMessage("CMD", params));
    }
    for (std::string key : {"", "+", "x;y", "x=y", "has space", "a/b/c", "x\r"}) {
        const std::array tags{ircbot::Tag{key, "value"}};
        EXPECT_FALSE(SerializeMessage("CMD", {}, tags));
    }
    const std::array duplicate{ircbot::Tag{"x", "a"}, ircbot::Tag{"x", "b"}};
    EXPECT_FALSE(SerializeMessage("CMD", {}, duplicate));
    const std::array nul{ircbot::Tag{"x", std::string("x\0y", 3)}};
    EXPECT_FALSE(SerializeMessage("CMD", {}, nul));
    const std::array invalid_utf8{ircbot::Tag{"x", "\xff"}};
    EXPECT_FALSE(SerializeMessage("CMD", {}, invalid_utf8));
    const std::array<std::string_view, 16> too_many{};
    EXPECT_FALSE(SerializeMessage("CMD", too_many));
}

TEST(IrcMessage, BoundsEscapedTagsAndOutgoingBodySeparately) {
    std::string text(503, 'x');
    std::array<std::string_view, 1> params{text};
    std::array tags{ircbot::Tag{"x", std::string(4092, 'y')}};
    auto line = SerializeMessage("NOTICE", params, tags);
    ASSERT_TRUE(line);
    EXPECT_EQ(line->size(), 4096u + 510u);
    text += 'x';
    params[0] = text;
    EXPECT_FALSE(SerializeMessage("NOTICE", params, tags));
    tags[0].value += 'y';
    EXPECT_FALSE(SerializeMessage("CMD", {}, tags));
    tags[0].value.assign(2046, ' ');
    EXPECT_TRUE(SerializeMessage("CMD", {}, tags));
    tags[0].value += ' ';
    EXPECT_FALSE(SerializeMessage("CMD", {}, tags));
}
} // namespace
