#include <gtest/gtest.h>
#include "Util/TimeFunc.hpp"

#include <chrono>
#include <format>

TEST(TimeTest, CurrentTime) {

    auto res = util::time::getCurrentTimestamp();

    ASSERT_FALSE(res.empty());

}

TEST(TimeTest, TimestampAfterNowPlusSupportsGeneratedUtcFormat) {
    auto future = util::time::getCurrentTimeAfterSeconds(600);

    EXPECT_TRUE(util::time::isTimestampAfterNowPlus(future, 300));
    EXPECT_FALSE(util::time::isTimestampAfterNowPlus(future, 900));
}

TEST(TimeTest, TimestampAfterNowPlusSupportsSupabaseOffsetFormat) {
    auto future = std::chrono::floor<std::chrono::seconds>(
        std::chrono::system_clock::now() + std::chrono::seconds{600}
    );
    auto timestamp = std::format("{:%FT%T+00:00}", future);

    EXPECT_TRUE(util::time::isTimestampAfterNowPlus(timestamp, 300));
    EXPECT_FALSE(util::time::isTimestampAfterNowPlus(timestamp, 900));
}

TEST(TimeTest, TimestampAfterNowPlusRejectsInvalidTimestamp) {
    EXPECT_FALSE(util::time::isTimestampAfterNowPlus("invalid", 300));
}
