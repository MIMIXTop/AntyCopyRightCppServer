//
// Created by mimixtop on 06.05.2026.
//

#include "DataBaseSession.hpp"

#include <boost/beast.hpp>
#include <boost/beast/http/message.hpp>

#include <format>
#include <iostream>
#include <print>

namespace {
    namespace asio = boost::asio;
    namespace beast = boost::beast;
    namespace http = beast::http;

    void setSupabaseHeaders(http::request<http::string_body>& request, const Util::ConfigParser& config) {
        request.set(http::field::authorization, config["SUPABASE_KEY"]);
        request.set("apikey", config["SUPABASE_KEY"]);
        request.set(http::field::host, config["SUPABASE_HOST"]);
        request.set(http::field::content_type, "application/json");
    }

    bool isWriteSuccess(http::status status) {
        return status == http::status::ok || status == http::status::created || status == http::status::no_content;
    }

    template <typename Body>
    asio::awaitable<http::response<Body>> sendDatabaseRequest(
        asio::any_io_executor executor,
        http::request<http::string_body> req) {
        auto session = std::make_shared<Network::SslSession>(executor);
        co_return co_await session->sendRequest<Body>(std::move(req));
    }

    std::optional<std::string> optionalString(const boost::json::object& json, std::string_view key) {
        auto value = json.if_contains(key);
        if (value == nullptr || value->is_null()) {
            return std::nullopt;
        }
        return std::string(value->as_string());
    }

    Network::AuthUser authUserFromJson(const boost::json::object& json) {
        return Network::AuthUser {
            .id = std::string(json.at("id").as_string()),
            .googleSub = std::string(json.at("google_sub").as_string()),
            .email = std::string(json.at("email").as_string()),
            .name = optionalString(json, "name").value_or(""),
            .pictureUrl = optionalString(json, "picture_url").value_or(""),
        };
    }

    Network::GoogleOAuthTokens googleOAuthTokensFromJson(const boost::json::object& json) {
        return Network::GoogleOAuthTokens {
            .userId = std::string(json.at("user_id").as_string()),
            .accessTokenEnc = std::string(json.at("access_token_enc").as_string()),
            .refreshTokenEnc = optionalString(json, "refresh_token_enc"),
            .expiresAt = std::string(json.at("expires_at").as_string()),
            .scope = optionalString(json, "scope").value_or(""),
            .tokenType = optionalString(json, "token_type").value_or(""),
        };
    }

    Network::AppSession appSessionFromJson(const boost::json::object& json) {
        return Network::AppSession {
            .id = std::string(json.at("id").as_string()),
            .userId = std::string(json.at("user_id").as_string()),
            .sessionHash = std::string(json.at("session_hash").as_string()),
            .expiresAt = std::string(json.at("expires_at").as_string()),
            .revokedAt = optionalString(json, "revoked_at"),
        };
    }
}

namespace Network {

DataBaseSession::DataBaseSession() = default;

asio::awaitable<bool> DataBaseSession::insertDocument(const Document& document) {
    boost::json::object documentJson {
        {"external_id", document.docId},
        {"title", document.text.empty() ? document.docId : document.text.front().title},
    };

    http::request<http::string_body> req{http::verb::post,  baseUrl + "/documents?select=id", 11};

    req.body() = boost::json::serialize(documentJson);
    req.set("Prefer", "return=representation");

    setSupabaseHeaders(req, config);
    req.prepare_payload();

    auto res = co_await sendDatabaseRequest<http::string_body>(threadPool.get_executor(), std::move(req));
    if (auto status = res.result(); status != http::status::created && status != http::status::ok) {
        co_return false;
    }

    auto insertedDocuments = boost::json::parse(res.body()).as_array();
    if (insertedDocuments.empty()) {
        co_return false;
    }

    auto const& documentId = insertedDocuments.front().as_object().at("id").as_string();

    if (document.text.empty()) {
        co_return true;
    }

    boost::json::array sectionsJson;
    sectionsJson.reserve(document.text.size());

    for (auto const& paragraph : document.text) {
        sectionsJson.emplace_back(boost::json::object {
            {"document_id", documentId},
            {"title", paragraph.title},
            {"content", paragraph.text},
        });
    }

    http::request<http::string_body> sectionsReq{http::verb::post,  baseUrl + "/document_sections", 11};

    sectionsReq.body() = boost::json::serialize(sectionsJson);
    sectionsReq.set("Prefer", "return=representation");
    setSupabaseHeaders(sectionsReq, config);
    sectionsReq.prepare_payload();

    auto sectionsRes = co_await sendDatabaseRequest<http::string_body>(threadPool.get_executor(), std::move(sectionsReq));
    if (auto status = sectionsRes.result(); status != http::status::created && status != http::status::ok) {
        co_return false;
    }

    co_return true;
}

asio::awaitable<std::optional<Document>> DataBaseSession::selectDocumentById(std::string_view documentId) {
    std::string target = std::format(
        "/rest/v1/documents?external_id=eq.{}&select=id,external_id,title,document_sections(id,title,content)",
        documentId
    );

    http::request<http::string_body> requestToGetListDocumentSections{http::verb::get, target, 11};
    requestToGetListDocumentSections.set("Prefer", "return=representation");
    setSupabaseHeaders(requestToGetListDocumentSections, config);
    requestToGetListDocumentSections.prepare_payload();

    auto resToDocumentSections = co_await sendDatabaseRequest<http::string_body>(threadPool.get_executor(), std::move(requestToGetListDocumentSections));

    if (const auto status = resToDocumentSections.result(); status != http::status::ok) {
        co_return std::nullopt;
    }

    auto json = boost::json::parse(resToDocumentSections.body());
    auto const& documents = json.as_array();

    if (documents.empty()) {
        co_return std::nullopt;
    }

    co_return boost::json::value_to<Document>(documents.front());

}
asio::awaitable<bool> DataBaseSession::deleteDocument(std::string_view documentId) {
    std::string target = std::format(
        "/rest/v1/documents?external_id=eq.{}",
        documentId
    );

    http::request<http::string_body> req {http::verb::delete_, target, 11};
    req.set("Prefer", "return=representation");
    setSupabaseHeaders(req, config);
    req.prepare_payload();

    auto res = co_await sendDatabaseRequest<http::string_body>(threadPool.get_executor(), std::move(req));
    if (auto status = res.result(); status != http::status::ok && status != http::status::no_content) {
        co_return false;
    }
    co_return true;
}
asio::awaitable<bool> DataBaseSession::insertOAuthState(std::string_view stateHash, std::string_view expiresAt) {
    std::string target = std::format(
        "/rest/v1/oauth_states"
    );

    http::request<http::string_body> req{http::verb::post, target, 11};
    setSupabaseHeaders(req, config);
    boost::json::object json;

    json["state_hash"] = stateHash;
    json["expires_at"] = expiresAt;
    req.body() = boost::json::serialize(json);
    req.prepare_payload();

    auto res = co_await sendDatabaseRequest<http::string_body>(threadPool.get_executor(), std::move(req));
    if (auto status = res.result(); status != http::status::created && status != http::status::ok) {
        co_return false;
    }
    co_return true;
}

asio::awaitable<bool>
DataBaseSession::consumeOAuthState(std::string_view stateHash, std::string_view consumedAt) {
    std::string target = std::format(
    "/rest/v1/oauth_states?state_hash=eq.{}&consumed_at=is.null&expires_at=gt.{}" ,
    stateHash,
    consumedAt
    );

    http::request<http::string_body> req{http::verb::patch, target, 11};
    req.set("Prefer", "return=representation");
    setSupabaseHeaders(req, config);

    boost::json::object json;
    json["consumed_at"] = consumedAt;
    req.body() = boost::json::serialize(json);
    req.prepare_payload();

    auto res = co_await sendDatabaseRequest<http::string_body>(threadPool.get_executor(), std::move(req));
    if (auto status = res.result(); status != http::status::created && status != http::status::ok) {
        co_return false;
    }
    auto body = boost::json::parse(res.body());

    auto arr = body.as_array();
    if (arr.empty()) {
        co_return false;
    }
    co_return true;

}
asio::awaitable<std::optional<AuthUser>> DataBaseSession::selectAuthUserByGoogleSub(std::string_view googleSub) {
    std::string target = std::format(
        "/rest/v1/auth_users?google_sub=eq.{}&select=id,google_sub,email,name,picture_url",
        googleSub
    );

    http::request<http::string_body> req{http::verb::get, target, 11};
    setSupabaseHeaders(req, config);
    req.set("Prefer", "return=representation");
    req.prepare_payload();

    auto res = co_await sendDatabaseRequest<http::string_body>(threadPool.get_executor(), std::move(req));
    if (res.result() != http::status::ok) {
        co_return std::nullopt;
    }

    auto users = boost::json::parse(res.body()).as_array();
    if (users.empty()) {
        co_return std::nullopt;
    }

    co_return authUserFromJson(users.front().as_object());
}

asio::awaitable<std::optional<AuthUser>> DataBaseSession::insertAuthUser(
    std::string_view googleSub, std::string_view email, std::string_view name, std::string_view pictureUrl,
    std::string_view lastLoginAt) {

    std::string target = std::format(
        "/rest/v1/auth_users?select=id,google_sub,email,name,picture_url"
    );

    http::request<http::string_body> req{http::verb::post, target, 11};
    setSupabaseHeaders(req, config);

    boost::json::object json;
    json["google_sub"] = googleSub;
    json["email"] = email;
    json["name"] = name;
    json["picture_url"] = pictureUrl;
    json["last_login_at"] = lastLoginAt;

    req.body() = boost::json::serialize(json);
    req.set("Prefer", "return=representation");
    req.prepare_payload();

    auto res = co_await sendDatabaseRequest<http::string_body>(threadPool.get_executor(), std::move(req));

    if (auto status = res.result(); status != http::status::created && status != http::status::ok) {
        std::println(
            std::cerr,
            "insertAuthUser failed: status={}, body={}",
            static_cast<unsigned>(status),
            res.body());
        co_return std::nullopt;
    }

    auto users = boost::json::parse(res.body()).as_array();
    if (users.empty()) {
        co_return std::nullopt;
    }

    co_return authUserFromJson(users.front().as_object());
}

asio::awaitable<std::optional<AuthUser>> DataBaseSession::updateAuthUserLogin(
    std::string_view id, std::string_view email, std::string_view name, std::string_view pictureUrl,
    std::string_view lastLoginAt) {
    std::string target = std::format(
        "/rest/v1/auth_users?id=eq.{}&select=id,google_sub,email,name,picture_url",
        id
    );

    http::request<http::string_body> req{http::verb::patch, target, 11};
    setSupabaseHeaders(req, config);

    boost::json::object json;
    json["email"] = email;
    json["name"] = name;
    json["picture_url"] = pictureUrl;
    json["last_login_at"] = lastLoginAt;
    json["updated_at"] = lastLoginAt;

    req.body() = boost::json::serialize(json);
    req.set("Prefer", "return=representation");
    req.prepare_payload();

    auto res = co_await sendDatabaseRequest<http::string_body>(threadPool.get_executor(), std::move(req));
    if (res.result() != http::status::ok) {
        co_return std::nullopt;
    }

    auto users = boost::json::parse(res.body()).as_array();
    if (users.empty()) {
        co_return std::nullopt;
    }

    co_return authUserFromJson(users.front().as_object());
}

asio::awaitable<bool> DataBaseSession::upsertGoogleOAuthTokens(const GoogleOAuthTokens& tokens) {
    http::request<http::string_body> req{
        http::verb::post,
        "/rest/v1/google_oauth_tokens?on_conflict=user_id",
        11
    };
    setSupabaseHeaders(req, config);
    req.set("Prefer", "resolution=merge-duplicates");

    boost::json::object json;
    json["user_id"] = tokens.userId;
    json["access_token_enc"] = tokens.accessTokenEnc;
    if (tokens.refreshTokenEnc.has_value()) {
        json["refresh_token_enc"] = *tokens.refreshTokenEnc;
    }
    json["expires_at"] = tokens.expiresAt;
    json["scope"] = tokens.scope;
    json["token_type"] = tokens.tokenType;

    req.body() = boost::json::serialize(json);
    req.prepare_payload();

    auto res = co_await sendDatabaseRequest<http::string_body>(threadPool.get_executor(), std::move(req));
    if (!isWriteSuccess(res.result())) {
        std::println(
            std::cerr,
            "upsertGoogleOAuthTokens failed: status={}, body={}",
            static_cast<unsigned>(res.result()),
            res.body());
        co_return false;
    }
    co_return true;
}

asio::awaitable<std::optional<GoogleOAuthTokens>> DataBaseSession::selectGoogleOAuthTokens(std::string_view userId) {
    std::string target = std::format(
        "/rest/v1/google_oauth_tokens?user_id=eq.{}&select=user_id,access_token_enc,refresh_token_enc,expires_at,scope,token_type",
        userId
    );

    http::request<http::string_body> req{http::verb::get, target, 11};
    setSupabaseHeaders(req, config);
    req.prepare_payload();

    auto res = co_await sendDatabaseRequest<http::string_body>(threadPool.get_executor(), std::move(req));
    if (res.result() != http::status::ok) {
        co_return std::nullopt;
    }

    auto rows = boost::json::parse(res.body()).as_array();
    if (rows.empty()) {
        co_return std::nullopt;
    }

    co_return googleOAuthTokensFromJson(rows.front().as_object());
}

asio::awaitable<bool> DataBaseSession::insertAppSession(
    std::string_view id,
    std::string_view userId,
    std::string_view sessionHash,
    std::string_view expiresAt,
    std::string_view userAgent) {
    http::request<http::string_body> req{http::verb::post, "/rest/v1/app_sessions", 11};
    setSupabaseHeaders(req, config);

    boost::json::object json;
    if (!id.empty()) {
        json["id"] = id;
    }
    json["user_id"] = userId;
    json["session_hash"] = sessionHash;
    json["expires_at"] = expiresAt;
    json["user_agent"] = userAgent;

    req.body() = boost::json::serialize(json);
    req.prepare_payload();

    auto res = co_await sendDatabaseRequest<http::string_body>(threadPool.get_executor(), std::move(req));
    if (!isWriteSuccess(res.result())) {
        std::println(
            std::cerr,
            "insertAppSession failed: status={}, body={}",
            static_cast<unsigned>(res.result()),
            res.body());
        co_return false;
    }
    co_return true;
}

asio::awaitable<std::optional<AppSession>> DataBaseSession::selectActiveAppSession(
    std::string_view sessionHash,
    std::string_view now) {
    std::string target = std::format(
        "/rest/v1/app_sessions?session_hash=eq.{}&revoked_at=is.null&expires_at=gt.{}&select=id,user_id,session_hash,expires_at,revoked_at",
        sessionHash,
        now
    );

    http::request<http::string_body> req{http::verb::get, target, 11};
    setSupabaseHeaders(req, config);
    req.prepare_payload();

    auto res = co_await sendDatabaseRequest<http::string_body>(threadPool.get_executor(), std::move(req));
    if (res.result() != http::status::ok) {
        co_return std::nullopt;
    }

    auto rows = boost::json::parse(res.body()).as_array();
    if (rows.empty()) {
        co_return std::nullopt;
    }

    co_return appSessionFromJson(rows.front().as_object());
}

asio::awaitable<bool> DataBaseSession::revokeAppSession(std::string_view sessionHash, std::string_view revokedAt) {
    std::string target = std::format("/rest/v1/app_sessions?session_hash=eq.{}", sessionHash);

    http::request<http::string_body> req{http::verb::patch, target, 11};
    setSupabaseHeaders(req, config);

    boost::json::object json;
    json["revoked_at"] = revokedAt;

    req.body() = boost::json::serialize(json);
    req.prepare_payload();

    auto res = co_await sendDatabaseRequest<http::string_body>(threadPool.get_executor(), std::move(req));
    co_return isWriteSuccess(res.result());
}

asio::awaitable<bool> DataBaseSession::updateAppSessionLastSeen(
    std::string_view sessionHash,
    std::string_view lastSeenAt) {
    std::string target = std::format("/rest/v1/app_sessions?session_hash=eq.{}", sessionHash);

    http::request<http::string_body> req{http::verb::patch, target, 11};
    setSupabaseHeaders(req, config);

    boost::json::object json;
    json["last_seen_at"] = lastSeenAt;

    req.body() = boost::json::serialize(json);
    req.prepare_payload();

    auto res = co_await sendDatabaseRequest<http::string_body>(threadPool.get_executor(), std::move(req));
    co_return isWriteSuccess(res.result());
}

asio::awaitable<std::optional<AuthUser>> DataBaseSession::selectAuthUserById(std::string_view userId) {
    auto target = std::format(
    "/rest/v1/auth_users?id=eq.{}&select=*", userId
    );

    http::request<http::string_body> req{http::verb::get, target, 11};
    setSupabaseHeaders(req, config);
    req.set("Prefer", "return=representation");
    req.prepare_payload();

    auto res = co_await sendDatabaseRequest<http::string_body>(threadPool.get_executor(), std::move(req));
    if (res.result() != http::status::ok) {
        co_return std::nullopt;
    }

    auto users = boost::json::parse(res.body()).as_array();
    if (users.empty()) {
        co_return std::nullopt;
    }

    co_return authUserFromJson(users.front().as_object());
}

}   // namespace Network
