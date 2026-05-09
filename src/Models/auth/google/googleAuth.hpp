#pragma once

#include <boost/json.hpp>

#include <string>
#include <optional>

namespace Network {
struct GoogleTokenResponse {
    std::string accessToken;
    std::optional<std::string> refreshToken;
    std::optional<std::string> idToken;
    std::string tokenType;
    std::string scope;
    int expiresIn = 0;
};

struct GoogleUserInfo {
    std::string sub;
    std::string email;
    std::string name;
    std::string pictureUrl;
};

inline void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, GoogleUserInfo const& userInfo) {
    jv = {
        {"sub", userInfo.sub},
        {"picture", userInfo.pictureUrl},
        {"email", userInfo.email},
        {"name", userInfo.name}
    };
}

}
