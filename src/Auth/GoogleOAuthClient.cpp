#include "Auth/GoogleOAuthClient.hpp"

#include "Util/Encrypt.hpp"

#include <boost/beast.hpp>

#include <stdexcept>
#include <format>
#include <boost/json.hpp>

namespace {
    namespace asio = boost::asio;
    namespace beast = boost::beast;
    namespace  http = beast::http;

    std::optional<std::string> optionalString(const boost::json::object& json, std::string_view key) {
        auto value = json.if_contains(key);
        if (value == nullptr || value->is_null()) {
            return std::nullopt;
        }
        return std::string(value->as_string());
    }
}

namespace Network {

GoogleOAuthClient::GoogleOAuthClient(asio::any_io_executor executor)
    : executor_(executor) {

}

asio::awaitable<GoogleTokenResponse> GoogleOAuthClient::exchangeCodeForTokens(std::string_view code) {
    if (config["GOOGLE_CLIENT_ID"].empty() || config["GOOGLE_CLIENT_SECRET"].empty() ||
        config["GOOGLE_REDIRECT_URI"].empty()) {
        throw std::logic_error("Google OAuth config is missing");
    }

    std::string body = util::urlEncodeHelper({
         {"grant_type", "authorization_code"},
         {"code", code},
         {"client_id", config["GOOGLE_CLIENT_ID"]},
         {"client_secret", config["GOOGLE_CLIENT_SECRET"]},
         {"redirect_uri", config["GOOGLE_REDIRECT_URI"]}
    });

    http::request<http::string_body> req{http::verb::post, "/token", 11};
    req.set(http::field::host, "oauth2.googleapis.com");
    req.set(http::field::content_type, "application/x-www-form-urlencoded");
    req.body() = body;
    req.prepare_payload();

    auto session = std::make_shared<SslSession>(executor_);
    auto res = co_await session->sendRequest<http::string_body>(std::move(req));

    if (res.result() != http::status::ok) {
        throw std::logic_error(std::format(
            "Google token exchange failed: status={}, body={}",
            static_cast<unsigned>(res.result()),
            res.body()));
    }

    auto json = boost::json::parse(res.body()).as_object();

    GoogleTokenResponse tokenResponse{
        .accessToken = std::string(json["access_token"].as_string()),
        .refreshToken = optionalString(json, "refresh_token"),
        .idToken = optionalString(json, "id_token"),
        .tokenType = std::string(json["token_type"].as_string()),
        .scope = optionalString(json, "scope").value_or(""),
        .expiresIn = static_cast<int>(json["expires_in"].as_int64()),
    };

    co_return tokenResponse;
}
asio::awaitable<GoogleTokenResponse> GoogleOAuthClient::refreshAccessToken(std::string_view refreshToken) {
    if (config["GOOGLE_CLIENT_ID"].empty() || config["GOOGLE_CLIENT_SECRET"].empty()) {
        throw std::logic_error("Google OAuth config is missing");
    }

    std::string body = util::urlEncodeHelper({
         {"grant_type", "refresh_token"},
         {"client_id", config["GOOGLE_CLIENT_ID"]},
         {"client_secret", config["GOOGLE_CLIENT_SECRET"]},
         {"refresh_token", refreshToken}
    });

    http::request<http::string_body> req{http::verb::post, "/token", 11};
    req.set(http::field::host, "oauth2.googleapis.com");
    req.set(http::field::content_type, "application/x-www-form-urlencoded");
    req.body() = body;
    req.prepare_payload();

    auto session = std::make_shared<SslSession>(executor_);
    auto res = co_await session->sendRequest<http::string_body>(std::move(req));

    if (res.result() != http::status::ok) {
        throw std::logic_error(std::format(
            "Google token exchange failed: status={}, body={}",
            static_cast<unsigned>(res.result()),
            res.body()));
    }

    auto json = boost::json::parse(res.body()).as_object();

    GoogleTokenResponse tokenResponse{
        .accessToken = std::string(json["access_token"].as_string()),
        .refreshToken = optionalString(json, "refresh_token"),
        .idToken = optionalString(json, "id_token"),
        .tokenType = std::string(json["token_type"].as_string()),
        .scope = optionalString(json, "scope").value_or(""),
        .expiresIn = static_cast<int>(json["expires_in"].as_int64()),
    };

    co_return tokenResponse;
}

asio::awaitable<GoogleUserInfo> GoogleOAuthClient::fetchUserInfo(std::string_view accessToken) {
    http::request<http::string_body> req{http::verb::get, "/v1/userinfo", 11};
    req.set(http::field::host, "openidconnect.googleapis.com");
    req.set(http::field::content_type, "application/json");
    req.set(http::field::authorization, std::format("Bearer {}", accessToken));
    req.prepare_payload();

    auto session = std::make_shared<SslSession>(executor_);
    auto res = co_await session->sendRequest<http::string_body>(std::move(req));
    if (res.result() != http::status::ok) {
        throw std::logic_error(std::format(
            "Google userinfo failed: status={}, body={}",
            static_cast<unsigned>(res.result()),
            res.body()));
    }

    auto json = boost::json::parse(res.body()).as_object();

    GoogleUserInfo userInfo{
        .sub = std::string(json["sub"].as_string()),
        .email = optionalString(json, "email").value_or(""),
        .name = optionalString(json, "name").value_or(""),
        .pictureUrl = optionalString(json, "picture").value_or(""),
    };

    co_return userInfo;
}

} // namespace Network
