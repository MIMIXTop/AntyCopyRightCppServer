#pragma once

#include "Models/Document.hpp"
#include "Models/auth/google/googleAuth.hpp"
#include "Models/data/DatabaseModels.hpp"

#include <memory>
#include <string_view>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/thread_pool.hpp>

#include <pqxx/pqxx>

namespace Network::Data
{
    class Database : public std::enable_shared_from_this<Database>
    {
    public:
        Database(std::string_view connectionString);
        std::shared_ptr<Database> get_ptr();

        boost::asio::awaitable<bool> insertDocument(Document document);
        boost::asio::awaitable<bool> deleteDocument(std::string_view documentId);
        boost::asio::awaitable<std::optional<Document>> selectDocument(std::string documentId);

        boost::asio::awaitable<bool> insertOAuthState(std::string_view stateHash, int64_t telegram_id, std::string_view expiresAt);
        boost::asio::awaitable<std::optional<int64_t>> consumeOAuthState(std::string_view stateHash, std::string_view consumedId);

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

        boost::asio::awaitable<std::optional<Type::AuthUser>> registerNewAuthUserWithTokens(
            std::string_view googleSub,
            std::string_view email,
            std::string_view name,
            std::string_view pictureUrl,
            std::string_view lastLoginAt,
            const Type::GoogleOAuthTokens& tokens);

        boost::asio::awaitable<bool> updateAuthUserLoginWithTokens(
            std::string_view id,
            std::string_view email,
            std::string_view name,
            std::string_view pictureUrl,
            std::string_view lastLoginAt,
            const Type::GoogleOAuthTokens& tokens);

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

        boost::asio::awaitable<std::optional<std::tuple<Models::DocumentFragment, Models::DocumentFragment>>> selectTwoDocumentFragments(std::string_view firstDocId, std::string_view secondDocId, std::string_view fragmentName);

        boost::asio::awaitable<bool> linkTelegramIdToUser(std::string_view userId, int64_t telegramId);
        boost::asio::awaitable<std::optional<Type::AuthUser>> selectAuthUserByTelegramId(int64_t telegramId);
    private:
        std::unique_ptr<pqxx::connection> getConnection();
        boost::asio::thread_pool pool_;

        std::string connectionString_;
    };
}
