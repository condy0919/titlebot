#include "policy.hpp"

#include <gtest/gtest.h>
namespace {
class IrcPolicy : public testing::Test {};
TEST_F(IrcPolicy, LimitsSessionAndTargetSeparately) {
    tb::IrcReplyPolicy policy({.per_target = 1, .per_session = 2});
    EXPECT_TRUE(policy.Allow("#a", {}));
    EXPECT_FALSE(policy.Allow("#A", {}));
    EXPECT_TRUE(policy.Allow("#b", {}));
    EXPECT_FALSE(policy.Allow("#c", {}));
}
TEST_F(IrcPolicy, KeepsCompleteUtf8AndSanitizesControls) {
    EXPECT_EQ(tb::FormatReplyLine("#a", "a\n中", 15), "PRIVMSG #a :a ");
}
TEST_F(IrcPolicy, UsesAdvertisedCaseMappingForTargetLimits) {
    tb::IrcReplyPolicy ascii({.per_target = 1});
    EXPECT_TRUE(ascii.Allow("#room[", {}, ircbot::CaseMapping::kAscii));
    EXPECT_TRUE(ascii.Allow("#room{", {}, ircbot::CaseMapping::kAscii));
    EXPECT_FALSE(ascii.Allow("#ROOM[", {}, ircbot::CaseMapping::kAscii));

    tb::IrcReplyPolicy strict({.per_target = 1});
    EXPECT_TRUE(strict.Allow("#room^", {}, ircbot::CaseMapping::kStrictRfc1459));
    EXPECT_TRUE(strict.Allow("#room~", {}, ircbot::CaseMapping::kStrictRfc1459));
    EXPECT_TRUE(strict.Allow("#room[", {}, ircbot::CaseMapping::kStrictRfc1459));
    EXPECT_FALSE(strict.Allow("#room{", {}, ircbot::CaseMapping::kStrictRfc1459));
}
} // namespace
