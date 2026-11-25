#include "title_parser.hpp"

#include <gtest/gtest.h>

namespace {
class TitleParser : public testing::Test {
protected:
    tb::TitleParser parser;
};

TEST_F(TitleParser, ReadsTitle) {
    const std::string s = "before <title>this is title</title> after";
    ASSERT_EQ(parser.parse(s.begin(), s.end()).value(), "this is title");
}

TEST_F(TitleParser, HandlesMissingTitle) {
    const std::string s = "<head>nothing</head>";
    ASSERT_FALSE(parser.parse(s.begin(), s.end()).has_value());
}

TEST_F(TitleParser, HandlesAttributes) {
    const std::string s = "<title itemprop=\"name\">twitter</title>";
    ASSERT_EQ(parser.parse(s.begin(), s.end()).value(), "twitter");
}

TEST_F(TitleParser, HandlesWhitespace) {
    const std::string s = "<p>1 < script and 2 > 1</p><title >facebook</title>";
    ASSERT_EQ(parser.parse(s.begin(), s.end()).value(), "facebook");
}

TEST_F(TitleParser, RequiresCompleteClosingTagAtEverySplit) {
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

TEST_F(TitleParser, ReadsDeclaredCharsetAcrossChunks) {
    std::string html =
        "<meta content='text/html; charset=shift_jis' http-equiv='Content-Type'><title>x</title>";
    for (auto it = html.begin(); it != html.end(); ++it) {
        parser.parse(it, it + 1);
    }
    EXPECT_EQ(parser.DeclaredCharset(), "shift_jis");
}

TEST_F(TitleParser, FindsDirectCharsetAfterValuelessAttributes) {
    const std::string html = "<meta disabled / content='text/html; charset=shift_jis' "
                             "http-equiv=Content-Type charset = 'UTF-8'>";
    parser.parse(html.begin(), html.end());
    EXPECT_EQ(parser.DeclaredCharset(), "UTF-8");
}

TEST_F(TitleParser, SkipsNonDocumentTitles) {
    std::string html =
        "<!-- <title>comment</title> --><script>let x='<title>script</title>';</script>"
        "<style><title>style</title></style><svg><svg/><title>icon</title></svg>"
        "<span data-x='<title>attr</title>'></span><title /><<title>real</title>";
    EXPECT_EQ(parser.parse(html.begin(), html.end()), "real");
}

TEST_F(TitleParser, KeepsFirstCharsetBeyondDetectionSample) {
    std::string html =
        std::string(1500, ' ') +
        "<meta data-info='a>b' charset='shift_jis'><meta charset='utf-8'><title>x</title>";
    for (auto it = html.begin(); it != html.end(); ++it) {
        parser.parse(it, it + 1);
    }
    EXPECT_EQ(parser.sample().size(), 1024u);
    EXPECT_EQ(parser.DeclaredCharset(), "shift_jis");
}

TEST_F(TitleParser, RecoversAfterTextAndShortComments) {
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

TEST_F(TitleParser, IgnoresNestedTemplateMetadata) {
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
TEST_F(TitleParser, ReadsCharsetBeforeSelfClosingSlash) {
    for (std::string tag : {"<meta charset=utf-8/>",
                            "<meta content='text/html; charset=utf-8' http-equiv=Content-Type/>"}) {
        tb::TitleParser document;
        document.parse(tag.begin(), tag.end());
        EXPECT_EQ(document.DeclaredCharset(), "utf-8");
    }
}
TEST_F(TitleParser, IgnoresRawTextAndAcceptsClosingTagAttributes) {
    for (std::string tag : {"textarea", "xmp", "noframes", "noembed"}) {
        auto body = "<" + tag + "><title>fake</title></" + tag + "><title>real</title ignored>";
        tb::TitleParser document;
        EXPECT_EQ(document.parse(body.begin(), body.end()), "real");
    }
}
} // namespace
