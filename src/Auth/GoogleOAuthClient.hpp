#pragma once

#include "Session/SslSession.hpp"
#include "Util/ConfigParser.hpp"
#include "Models/auth/google/googleAuth.hpp"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace Network {


class GoogleOAuthClient {
public:
    explicit GoogleOAuthClient(boost::asio::any_io_executor executor);

    boost::asio::awaitable<GoogleTokenResponse> exchangeCodeForTokens(std::string_view code);
    boost::asio::awaitable<GoogleTokenResponse> refreshAccessToken(std::string_view refreshToken);
    boost::asio::awaitable<GoogleUserInfo> fetchUserInfo(std::string_view accessToken);

private:
    boost::asio::any_io_executor executor_;
    Util::ConfigParser config;
};

} // namespace Network
