#include "Util/Encrypt.hpp"

#include <gtest/gtest.h>

#include <ranges>
#include <algorithm>
#include <stdexcept>

TEST(EncryptTest, randomUrlSafeToken) {
    auto str = util::randomUrlSafeToken();

    ASSERT_FALSE(str.empty());

    using namespace std::string_view_literals;

    auto res = std::ranges::find_if(str, [](char ch) {
        return "+=/"sv.find(ch) != std::string_view::npos;
    });

    EXPECT_EQ(res, str.end());
}

TEST(EncryptTest, TextEncryptDecryptRoundTrip) {
    std::string token = "ya29.test-token-value";
    std::string key = "test-encryption-key";

    auto encrypted = util::textEncrypt(token, key);

    ASSERT_FALSE(encrypted.empty());
    EXPECT_NE(encrypted, token);
    EXPECT_EQ(util::textDecrypt(encrypted, key), token);
}

TEST(EncryptTest, TextEncryptUsesRandomIv) {
    std::string token = "same-token";
    std::string key = "test-encryption-key";

    auto encrypted1 = util::textEncrypt(token, key);
    auto encrypted2 = util::textEncrypt(token, key);

    EXPECT_NE(encrypted1, encrypted2);
    EXPECT_EQ(util::textDecrypt(encrypted1, key), token);
    EXPECT_EQ(util::textDecrypt(encrypted2, key), token);
}

TEST(EncryptTest, TextDecryptRejectsWrongKey) {
    auto encrypted = util::textEncrypt("secret-token", "right-key");

    EXPECT_THROW(
        static_cast<void>(util::textDecrypt(encrypted, "wrong-key")),
        std::runtime_error
    );
}

TEST(EncryptTest, TextEncryptRejectsEmptyKey) {
    EXPECT_THROW(
        static_cast<void>(util::textEncrypt("secret-token", "")),
        std::runtime_error
    );
}
