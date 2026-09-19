#pragma once
#include "Models/auth/google/googleAuth.hpp"
#include "Util/Encrypt.hpp"

#include "Services/Database/Database.hpp"
#include <Util/ConfigParser.hpp>

#include <memory>

namespace Network::Auth {
class GoogleTokenManager {
public:
    GoogleTokenManager(
        const boost::asio::any_io_executor &executor, const std::shared_ptr<Data::Database> &database, Util::ConfigParser& parser);

    boost::asio::awaitable<std::optional<std::string>> getValidAccessToken(std::string_view userId) const;

private:
    std::shared_ptr<Data::Database> databaseSession;
    Util::ConfigParser &config;
    boost::asio::any_io_executor executor;
};
}
