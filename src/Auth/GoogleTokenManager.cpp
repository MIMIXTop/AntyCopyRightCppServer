#include "GoogleTokenManager.hpp"
#include "GoogleOAuthClient.hpp"
#include "Util/Encrypt.hpp"
#include "Util/TimeFunc.hpp"

#include <iostream>
#include <print>

namespace {

namespace asio = boost::asio;

}

namespace  Network::Auth {

GoogleTokenManager::GoogleTokenManager(
    const asio::any_io_executor& executor, const std::shared_ptr<DataBaseSession>& database,
    Util::ConfigParser& parser)
: databaseSession(database) , config(parser), executor(executor) {}

asio::awaitable<std::optional<std::string>> GoogleTokenManager::getValidAccessToken(std::string_view userId) const {

    auto auth = co_await databaseSession->selectGoogleOAuthTokens(userId);
    if (auth == std::nullopt) {
        co_return std::nullopt;
    }

    std::string_view access_token = auth->accessTokenEnc;
    auto encrypt_key = std::string(config["TOKEN_ENCRYPTION_KEY"]);
    if (encrypt_key.empty()) {
        encrypt_key = std::string(config["SECRET_KEY"]);
    }
    std::string decrypt_access_token;
    try {
        decrypt_access_token = util::textDecrypt(access_token, encrypt_key);
    } catch (const std::exception& e) {
        std::println(std::cerr, "Failed to decrypt Google access token: {}", e.what());
        co_return std::nullopt;
    }

    if (!util::time::isTimestampAfterNowPlus(auth->expiresAt, 300)) {
        if (auth->refreshTokenEnc == std::nullopt) {
            co_return std::nullopt;
        }

        GoogleOAuthClient client{executor};
        std::string decrypt_refresh_token;
        try {
            decrypt_refresh_token = util::textDecrypt(auth->refreshTokenEnc.value(), encrypt_key);
        } catch (const std::exception& e) {
            std::println(std::cerr, "Failed to decrypt Google refresh token: {}", e.what());
            co_return std::nullopt;
        }

        GoogleTokenResponse tokenRes;
        try {
            tokenRes = co_await client.refreshAccessToken(decrypt_refresh_token);
        } catch (const std::exception& e) {
            std::println(std::cerr, "Failed to refresh Google access token: {}", e.what());
            co_return std::nullopt;
        }

        auto new_encrypt_access_token = util::textEncrypt(tokenRes.accessToken, encrypt_key);
        std::optional<std::string> new_encrypt_refresh_token;
        if (tokenRes.refreshToken == std::nullopt) {
            new_encrypt_refresh_token = auth->refreshTokenEnc;
        } else {
            new_encrypt_refresh_token = util::textEncrypt(tokenRes.refreshToken.value(), encrypt_key);
        }
        co_await databaseSession->upsertGoogleOAuthTokens({
            .userId = std::string(userId),
            .accessTokenEnc = new_encrypt_access_token,
            .refreshTokenEnc = new_encrypt_refresh_token,
            .expiresAt = util::time::getCurrentTimeAfterSeconds(tokenRes.expiresIn),
            .scope = tokenRes.scope,
            .tokenType =tokenRes.tokenType
        });

        co_return tokenRes.accessToken;
    }

    co_return decrypt_access_token;
}
}   // namespace Network::Auth
