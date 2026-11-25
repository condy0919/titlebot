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
TEST_F(IrcPolicy, KeepsSuffixWithinAdvertisedLimit) {
    tb::NicknameNegotiator names;
    names.Observe("bot NICKLEN=2 :supported");
    auto first = names.Retry("中文", "中文");
    auto second = names.Retry("中文", "中文");
    ASSERT_TRUE(first && second);
    EXPECT_EQ(first->size(), 2u);
    EXPECT_EQ(first->front(), '_');
    EXPECT_NE(first, second);
}
} // namespace
