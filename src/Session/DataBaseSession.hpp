#pragma once
#include "Session/SslSession.hpp"
#include "Models/Document.hpp"
#include "Util/ConfigParser.hpp"

#include <boost/asio/awaitable.hpp>

#include <memory>
#include <optional>
#include <string_view>

namespace Network {
class DataBaseSession {
public:
    DataBaseSession();

    boost::asio::awaitable<bool> insertDocument(const Document& document);
    boost::asio::awaitable<std::optional<Document>> selectDocumentById(std::string_view documentId);
    boost::asio::awaitable<bool> deleteDocument(std::string_view documentId);


private:
    boost::asio::thread_pool threadPool{
        std::max(1u, std::thread::hardware_concurrency() / 2)
    };
    std::shared_ptr<SslSession> databaseSession;
    Util::ConfigParser config;

    std::string baseUrl = "/rest/v1";
};
}
