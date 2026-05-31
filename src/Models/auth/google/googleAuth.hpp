#pragma once

#include <boost/json.hpp>

#include <string>
#include <optional>

namespace Network::Type
{
    struct GoogleTokenResponse
    {
        std::string accessToken;
        std::optional<std::string> refreshToken;
        std::optional<std::string> idToken;
        std::string tokenType;
        std::string scope;
        int expiresIn = 0;
    };

    struct AuthUser
    {
        std::string id;
        std::string googleSub;
        std::string email;
        std::string name;
        std::string pictureUrl;
    };

    struct GoogleOAuthTokens
    {
        std::string userId;
        std::string accessTokenEnc;
        std::optional<std::string> refreshTokenEnc;
        std::string expiresAt;
        std::string scope;
        std::string tokenType;
    };

    struct AppSession
    {
        std::string id;
        std::string userId;
        std::string sessionHash;
        std::string expiresAt;
        std::optional<std::string> revokedAt;
    };

    struct GoogleUserInfo
    {
        std::string sub;
        std::string email;
        std::string name;
        std::string pictureUrl;
    };

    inline void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, GoogleUserInfo const& userInfo)
    {
        jv = {
            {"sub", userInfo.sub},
            {"picture", userInfo.pictureUrl},
            {"email", userInfo.email},
            {"name", userInfo.name}
        };
    }
}
