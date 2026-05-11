#pragma once
#include "Models/auth/google/googleAuth.hpp"
#include "Util/Encrypt.hpp"

#include <Session/DataBaseSession.hpp>
#include <Util/ConfigParser.hpp>

#include <memory>

namespace Network::Auth {
class GoogleTokenManager {
public:
    GoogleTokenManager(
        const boost::asio::any_io_executor &executor, const std::shared_ptr<DataBaseSession> &database, Util::ConfigParser& parser);

    boost::asio::awaitable<std::optional<std::string>> getValidAccessToken(std::string_view userId) const;

private:
    std::shared_ptr<DataBaseSession> databaseSession;
    Util::ConfigParser &config;
    boost::asio::any_io_executor executor;
};
}
