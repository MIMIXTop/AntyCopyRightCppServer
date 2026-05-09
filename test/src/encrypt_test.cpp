#include "Util/Encrypt.hpp"

#include <gtest/gtest.h>

#include <ranges>
#include <algorithm>

TEST(EncryptTest, randomUrlSafeToken) {
    auto str = util::randomUrlSafeToken();

    ASSERT_FALSE(str.empty());

    using namespace std::string_view_literals;

    auto res = std::ranges::find_if(str, [](char ch) {
        return "+=/"sv.find(ch) != std::string_view::npos;
    });

    EXPECT_EQ(res, str.end());
}