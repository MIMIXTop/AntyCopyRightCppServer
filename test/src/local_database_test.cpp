#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <gtest/gtest.h>
#include "Database/Database.hpp"
#include "Models/auth/google/googleAuth.hpp"

using Network::Type::GoogleOAuthTokens;

class FixtureLocaleDatabase : public testing::Test {
protected:
    void SetUp() override {
        database = std::make_shared<Network::Data::Database>("host=localhost port=6432 dbname=db user=user password=pass");
    }

public:
    std::shared_ptr<Network::Data::Database> database;
};


TEST_F(FixtureLocaleDatabase, InitTest) {
    ASSERT_TRUE(database);
}

TEST_F(FixtureLocaleDatabase, InserLocaleTest) {
    ASSERT_TRUE(database);

    std::vector<Documents::Paragraph> paragraphs{
            {
                .title = "Введение",
                .text = "Текст первого раздела тестового документа.",
            },
            {
                .title = "Заключение",
                .text = "Текст второго раздела тестового документа.",
            },
        };

    Document document(std::move(paragraphs), "test-external-document-id", "Title");

    boost::asio::io_context io;
    auto databaseResponse =  boost::asio::co_spawn(
        io,
        database->insertDocument(document),
        boost::asio::use_future
    );
    io.run();
    EXPECT_TRUE(databaseResponse.get());
}

TEST_F(FixtureLocaleDatabase, SelectTest) {
    ASSERT_TRUE(database);

    boost::asio::io_context io;
    auto response =  boost::asio::co_spawn(
        io,
        database->selectDocument("test-external-document-id"),
        boost::asio::use_future
    );

    io.run();

    auto selectResult = response.get();

    ASSERT_TRUE(selectResult.has_value());

    auto&& document = selectResult.value();

    EXPECT_EQ("test-external-document-id", document.docId);

    auto&& documentText = document.text;

    EXPECT_EQ("Введение", documentText[0].title);
    EXPECT_EQ("Заключение", documentText[1].title);

    EXPECT_EQ("Текст первого раздела тестового документа.", documentText[0].text);
    EXPECT_EQ("Текст второго раздела тестового документа.", documentText[1].text);

}

TEST_F(FixtureLocaleDatabase, DeleteTest) {
    ASSERT_TRUE(database);

    boost::asio::io_context io;
    auto response =  boost::asio::co_spawn(
        io,
        database->deleteDocument("test-external-document-id"),
        boost::asio::use_future
    );

    io.run();

    EXPECT_TRUE(response.get());
}

TEST_F(FixtureLocaleDatabase, RegisterNewAuthUserWithTokensTest) {
    ASSERT_TRUE(database);

    GoogleOAuthTokens tokens{
        .userId = "",
        .accessTokenEnc = "encrypted_access_token_test",
        .refreshTokenEnc = "encrypted_refresh_token_test",
        .expiresAt = "2026-12-31T23:59:59Z",
        .scope = "openid email profile",
        .tokenType = "Bearer"
    };

    boost::asio::io_context io;
    auto result = boost::asio::co_spawn(
        io,
        database->registerNewAuthUserWithTokens(
            "google_sub_test_atomic",
            "atomic_test@example.com",
            "Atomic Test User",
            "https://example.com/atomic_photo.jpg",
            "2026-06-02T17:31:41Z",
            tokens
        ),
        boost::asio::use_future
    );
    io.run();

    auto authUser = result.get();
    ASSERT_TRUE(authUser.has_value());
    
    EXPECT_EQ("atomic_test@example.com", authUser->email);
    EXPECT_EQ("google_sub_test_atomic", authUser->googleSub);
    EXPECT_EQ("Atomic Test User", authUser->name);
    EXPECT_EQ("https://example.com/atomic_photo.jpg", authUser->pictureUrl);

    // Проверить что токены тоже созданы
    boost::asio::io_context io2;
    auto getTokensResult = boost::asio::co_spawn(
        io2,
        database->selectGoogleOAuthTokens(authUser->id),
        boost::asio::use_future
    );
    io2.run();

    auto savedTokens = getTokensResult.get();
    ASSERT_TRUE(savedTokens.has_value());
    EXPECT_EQ("encrypted_access_token_test", savedTokens->accessTokenEnc);
    EXPECT_EQ("encrypted_refresh_token_test", savedTokens->refreshTokenEnc);
}

TEST_F(FixtureLocaleDatabase, UpdateAuthUserLoginWithTokensTest) {
    ASSERT_TRUE(database);

    // Сначала создать пользователя
    GoogleOAuthTokens initialTokens{
        .userId = "",
        .accessTokenEnc = "initial_access_token",
        .refreshTokenEnc = "initial_refresh_token",
        .expiresAt = "2026-12-31T23:59:59Z",
        .scope = "openid",
        .tokenType = "Bearer"
    };

    boost::asio::io_context io;
    auto createResult = boost::asio::co_spawn(
        io,
        database->registerNewAuthUserWithTokens(
            "google_sub_update_test",
            "update_test@example.com",
            "Update Test User",
            "https://example.com/update_photo.jpg",
            "2026-06-02T17:00:00Z",
            initialTokens
        ),
        boost::asio::use_future
    );
    io.run();

    auto authUser = createResult.get();
    ASSERT_TRUE(authUser.has_value());

    // Теперь обновить пользователя с новыми токенами
    GoogleOAuthTokens updatedTokens{
        .userId = authUser->id,
        .accessTokenEnc = "updated_access_token",
        .refreshTokenEnc = "updated_refresh_token",
        .expiresAt = "2026-12-31T23:59:59Z",
        .scope = "openid email profile",
        .tokenType = "Bearer"
    };

    boost::asio::io_context io2;
    auto updateResult = boost::asio::co_spawn(
        io2,
        database->updateAuthUserLoginWithTokens(
            authUser->id,
            "updated_test@example.com",
            "Updated Test User",
            "https://example.com/updated_photo.jpg",
            "2026-06-02T17:30:00Z",
            updatedTokens
        ),
        boost::asio::use_future
    );
    io2.run();

    ASSERT_TRUE(updateResult.get());

    // Проверить обновленные токены
    boost::asio::io_context io3;
    auto getTokensResult = boost::asio::co_spawn(
        io3,
        database->selectGoogleOAuthTokens(authUser->id),
        boost::asio::use_future
    );
    io3.run();

    auto savedTokens = getTokensResult.get();
    ASSERT_TRUE(savedTokens.has_value());
    EXPECT_EQ("updated_access_token", savedTokens->accessTokenEnc);
    EXPECT_EQ("updated_refresh_token", savedTokens->refreshTokenEnc);
}
