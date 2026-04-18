#pragma once
#include <string>
#include <pqxx/pqxx>

namespace Network {
class Database {
public:
    Database(const std::string& connectionString);

private:

    pqxx::connection connection;
};
}   // namespace Network