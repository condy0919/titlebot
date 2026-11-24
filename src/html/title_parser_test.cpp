#include "title_parser.hpp"

#include <gtest/gtest.h>

TEST(TitleParser, ReadsTitle) {
    const std::string s = "before <title>this is title</title> after";
    tb::TitleParser parser;
    ASSERT_EQ(parser.parse(s.begin(), s.end()).value(), "this is title");
}

TEST(TitleParser, HandlesMissingTitle) {
    const std::string s = "<head>nothing</head>";
    tb::TitleParser parser;
    ASSERT_FALSE(parser.parse(s.begin(), s.end()).has_value());
}

TEST(TitleParser, HandlesAttributes) {
    const std::string s = "<title itemprop=\"name\">twitter</title>";
    tb::TitleParser parser;
    ASSERT_EQ(parser.parse(s.begin(), s.end()).value(), "twitter");
}

TEST(TitleParser, HandlesWhitespace) {
    const std::string s = "<  title >facebook</title>";
    tb::TitleParser parser;
    ASSERT_EQ(parser.parse(s.begin(), s.end()).value(), "facebook");
}

TEST(TitleParser, RequiresCompleteClosingTagAtEverySplit) {
    const std::string html =
        "<titlebar>ignored</titlebar><TITLE data-label='>'>中文 < 3 &amp; A</TiTlE  >";
    // 穷举切分点，覆盖多字节字符、属性引号和结束标签内部。
    for (size_t split = 0; split < html.size(); ++split) {
        tb::TitleParser parser;
        ASSERT_FALSE(parser.parse(html.begin(), html.begin() + split));
        EXPECT_EQ(parser.parse(html.begin() + split, html.end()), "中文 < 3 &amp; A");
    }
}
