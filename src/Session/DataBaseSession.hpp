#pragma once
#include "Session/SslSession.hpp"
#include "Models/Document.hpp"
#include "Util/ConfigParser.hpp"

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace Network {

struct AuthUser {
    std::string id;
    std::string googleSub;
    std::string email;
    std::string name;
    std::string pictureUrl;
};

struct GoogleOAuthTokens {
    std::string userId;
    std::string accessTokenEnc;
    std::optional<std::string> refreshTokenEnc;
    std::string expiresAt;
    std::string scope;
    std::string tokenType;
};

struct AppSession {
    std::string id;
    std::string userId;
    std::string sessionHash;
    std::string expiresAt;
    std::optional<std::string> revokedAt;
};

class DataBaseSession {
public:
    DataBaseSession();

    boost::asio::awaitable<bool> insertDocument(const Document& document);
    boost::asio::awaitable<std::optional<Document>> selectDocumentById(std::string_view documentId);
    boost::asio::awaitable<bool> deleteDocument(std::string_view documentId);

    boost::asio::awaitable<bool> insertOAuthState(std::string_view stateHash, std::string_view expiresAt);
    boost::asio::awaitable<bool> consumeOAuthState(std::string_view stateHash, std::string_view consumedAt);

    boost::asio::awaitable<std::optional<AuthUser>> selectAuthUserByGoogleSub(std::string_view googleSub);
    boost::asio::awaitable<std::optional<AuthUser>> insertAuthUser(
        std::string_view id,
        std::string_view googleSub,
        std::string_view email,
        std::string_view name,
        std::string_view pictureUrl,
        std::string_view lastLoginAt);
    boost::asio::awaitable<std::optional<AuthUser>> updateAuthUserLogin(
        std::string_view id,
        std::string_view email,
        std::string_view name,
        std::string_view pictureUrl,
        std::string_view lastLoginAt);

    boost::asio::awaitable<bool> upsertGoogleOAuthTokens(const GoogleOAuthTokens& tokens);
    boost::asio::awaitable<std::optional<GoogleOAuthTokens>> selectGoogleOAuthTokens(std::string_view userId);

    boost::asio::awaitable<bool> insertAppSession(
        std::string_view id,
        std::string_view userId,
        std::string_view sessionHash,
        std::string_view expiresAt,
        std::string_view userAgent);
    boost::asio::awaitable<std::optional<AppSession>> selectActiveAppSession(
        std::string_view sessionHash,
        std::string_view now);
    boost::asio::awaitable<bool> revokeAppSession(std::string_view sessionHash, std::string_view revokedAt);
    boost::asio::awaitable<bool> updateAppSessionLastSeen(std::string_view sessionHash, std::string_view lastSeenAt);


private:
    boost::asio::thread_pool threadPool{
        std::max(1u, std::thread::hardware_concurrency() / 2)
    };
    std::shared_ptr<SslSession> databaseSession;
    Util::ConfigParser config;

    std::string baseUrl = "/rest/v1";
};
}
