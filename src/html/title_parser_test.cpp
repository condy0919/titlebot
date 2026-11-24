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
    // Try every split point, including splits inside multibyte characters, quoted attributes and
    // closing tags.
    for (size_t split = 0; split < html.size(); ++split) {
        tb::TitleParser parser;
        ASSERT_FALSE(parser.parse(html.begin(), html.begin() + split));
        EXPECT_EQ(parser.parse(html.begin() + split, html.end()), "中文 < 3 &amp; A");
    }
}

TEST(TitleParser, ReadsDeclaredCharsetAcrossChunks) {
    tb::TitleParser parser;
    std::string html =
        "<meta content='text/html; charset=shift_jis' http-equiv='Content-Type'><title>x</title>";
    for (auto it = html.begin(); it != html.end(); ++it) {
        parser.parse(it, it + 1);
    }
    EXPECT_EQ(parser.DeclaredCharset(), "shift_jis");
}

TEST(TitleParser, SkipsNonDocumentTitles) {
    std::string html =
        "<!-- <title>comment</title> --><script>let x='<title>script</title>';</script>"
        "<style><title>style</title></style><svg><svg/><title>icon</title></svg>"
        "<span data-x='<title>attr</title>'></span><title /><<title>real</title>";
    tb::TitleParser parser;
    EXPECT_EQ(parser.parse(html.begin(), html.end()), "real");
}

TEST(TitleParser, KeepsFirstCharsetBeyondDetectionSample) {
    std::string html =
        std::string(1500, ' ') +
        "<meta data-info='a>b' charset='shift_jis'><meta charset='utf-8'><title>x</title>";
    tb::TitleParser parser;
    for (auto it = html.begin(); it != html.end(); ++it) {
        parser.parse(it, it + 1);
    }
    EXPECT_EQ(parser.sample().size(), 1024u);
    EXPECT_EQ(parser.DeclaredCharset(), "shift_jis");
}

TEST(TitleParser, RecoversAfterTextAndShortComments) {
    for (std::string prefix : {"<noscript>1 < 2, it's ok</noscript>", "<!-->", "<!--->",
                               "<title / >", "<!-- ordinary > <title>ignore</title> -->"}) {
        auto html = prefix + "<title>real</title>";
        tb::TitleParser parser;
        std::optional<std::string> result;
        for (auto it = html.begin(); it != html.end() && !result; ++it) {
            result = parser.parse(it, it + 1);
        }
        EXPECT_EQ(result, "real") << prefix;
    }
}

TEST(TitleParser, IgnoresNestedTemplateMetadata) {
    std::string prefix = "<template><template><meta charset='shift_jis'></template>"
                         "<title>占位</title></template>";
    tb::TitleParser only;
    EXPECT_FALSE(only.parse(prefix.begin(), prefix.end()));
    std::string html = prefix + "<meta charset='utf-8'><title>首页</title>";
    for (size_t split = 0; split < html.size(); ++split) {
        tb::TitleParser parser;
        EXPECT_FALSE(parser.parse(html.begin(), html.begin() + split));
        EXPECT_EQ(parser.parse(html.begin() + split, html.end()), "首页");
        EXPECT_EQ(parser.DeclaredCharset(), "utf-8");
    }
}
