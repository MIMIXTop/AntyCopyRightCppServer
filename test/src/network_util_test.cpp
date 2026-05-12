#include <gtest/gtest.h>
#include "Util/NetworkHealper.hpp"

TEST(NetworkUtils, VerifPath) {
    const boost::url_view targetVerif1 = "/api/classroom/courses/123/courseWork";
    const boost::url_view targetVerif2 = "/api/classroom/courses";
    const boost::url_view targetVerif3 = "/api/classroom/courses/123/students";
    const boost::url_view targetVerif4 = "/api/classroom/courses/123/courseWork/123/studentSubmissions";
    const boost::url_view targetNoVerif1 = "/api/classroom/courses/123/trololo";
    const boost::url_view targetNoVerif2 = "/api/classroom/courses/courseWork";
    const boost::url_view targetNoVerif3 = "/api/classroom/courses/trololo";
    const boost::url_view targetNoVerif4 = "/api/classroom/courses/123/trololo/23/studentSubmissions";

    ASSERT_TRUE(util::network::verifPath(targetVerif1));
    ASSERT_TRUE(util::network::verifPath(targetVerif2));
    ASSERT_TRUE(util::network::verifPath(targetVerif3));
    ASSERT_TRUE(util::network::verifPath(targetVerif4));
    ASSERT_FALSE(util::network::verifPath(targetNoVerif1));
    ASSERT_FALSE(util::network::verifPath(targetNoVerif2));
    ASSERT_FALSE(util::network::verifPath(targetNoVerif3));
    ASSERT_FALSE(util::network::verifPath(targetNoVerif4));
}

TEST(NetworkUtil, CookieParse) {
    {
        std::string_view cookie;
        auto parsed = util::network::parse_cookie(cookie);
        ASSERT_TRUE(parsed.empty());
    }

    {
        std::string_view cookie = "anty_session=session-token";
        auto parsed = util::network::parse_cookie(cookie);

        ASSERT_EQ(parsed.size(), 1);
        ASSERT_EQ(parsed.at("anty_session"), "session-token");
    }

    {
        std::string_view cookie = "anty_session=session-token; theme=dark; lang=ru";
        auto parsed = util::network::parse_cookie(cookie);

        ASSERT_EQ(parsed.size(), 3);
        ASSERT_EQ(parsed.at("anty_session"), "session-token");
        ASSERT_EQ(parsed.at("theme"), "dark");
        ASSERT_EQ(parsed.at("lang"), "ru");
    }

    {
        std::string_view cookie = "anty_session=session-token; theme=dark; invalid; empty=";
        auto parsed = util::network::parse_cookie(cookie);

        ASSERT_EQ(parsed.size(), 3);
        ASSERT_EQ(parsed.at("anty_session"), "session-token");
        ASSERT_EQ(parsed.at("theme"), "dark");
        ASSERT_EQ(parsed.at("empty"), "");
        ASSERT_FALSE(parsed.contains("invalid"));
    }

    {
        std::string_view cookie = "signed=value=with=equals";
        auto parsed = util::network::parse_cookie(cookie);

        ASSERT_EQ(parsed.size(), 1);
        ASSERT_EQ(parsed.at("signed"), "value=with=equals");
    }
}
