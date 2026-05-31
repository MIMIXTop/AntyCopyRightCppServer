#pragma once
#include "Session/SslSession.hpp"
#include "Models/Document.hpp"
#include "Util/ConfigParser.hpp"

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "../Models/auth/google/googleAuth.hpp"
#include "Models/auth/google/googleAuth.hpp"

namespace Network {



class DataBaseSession {
public:
    DataBaseSession();

    boost::asio::awaitable<bool> insertDocument(const Document& document);
    boost::asio::awaitable<std::optional<Document>> selectDocumentById(std::string_view documentId);
    boost::asio::awaitable<bool> deleteDocument(std::string_view documentId);

    boost::asio::awaitable<bool> insertOAuthState(std::string_view stateHash, std::string_view expiresAt);
    boost::asio::awaitable<bool> consumeOAuthState(std::string_view stateHash, std::string_view consumedAt);

    boost::asio::awaitable<std::optional<Type::AuthUser>> selectAuthUserByGoogleSub(std::string_view googleSub);
    boost::asio::awaitable<std::optional<Type::AuthUser>> insertAuthUser(
        std::string_view googleSub,
        std::string_view email,
        std::string_view name,
        std::string_view pictureUrl,
        std::string_view lastLoginAt);
    boost::asio::awaitable<std::optional<Type::AuthUser>> updateAuthUserLogin(
        std::string_view id,
        std::string_view email,
        std::string_view name,
        std::string_view pictureUrl,
        std::string_view lastLoginAt);

    boost::asio::awaitable<bool> upsertGoogleOAuthTokens(const Type::GoogleOAuthTokens& tokens);
    boost::asio::awaitable<std::optional<Type::GoogleOAuthTokens>> selectGoogleOAuthTokens(std::string_view userId);

    boost::asio::awaitable<bool> insertAppSession(
        std::string_view id,
        std::string_view userId,
        std::string_view sessionHash,
        std::string_view expiresAt,
        std::string_view userAgent);
    boost::asio::awaitable<std::optional<Type::AppSession>> selectActiveAppSession(
        std::string_view sessionHash,
        std::string_view now);
    boost::asio::awaitable<bool> revokeAppSession(std::string_view sessionHash, std::string_view revokedAt);
    boost::asio::awaitable<bool> updateAppSessionLastSeen(std::string_view sessionHash, std::string_view lastSeenAt);

    boost::asio::awaitable<std::optional<Type::AuthUser>> selectAuthUserById(std::string_view userId);

private:
    boost::asio::thread_pool threadPool{
        std::max(1u, std::thread::hardware_concurrency() / 2)
    };
    Util::ConfigParser config;

    std::string baseUrl = "/rest/v1";
};
}
