#pragma once

#include "Models/Paragraph.hpp"
#include "Models/Document.hpp"
#include "Session/DataBaseSession.hpp"
#include "Session/SslSession.hpp"
#include "Util/ConfigParser.hpp"

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/beast.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <string>
#include <unordered_map>

namespace Network {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;
using ssl_stream = asio::ssl::stream<beast::tcp_stream>;
using tcp_stream = beast::tcp_stream;

class Server {
public:
    Server(asio::io_context& io, const std::string& address, const std::string& port, const std::string& cert_file,
           const std::string& private_key_file);

    void start();

private:
    enum RequesType {
        GetStudentAnalizis,
        GetRefreshToken,
        AuthGoogleStart,
        AuthGoogleCallback,
        AuthMe,
        AuthLogout,
    };

    struct DocumentRequest {
        http::request<http::string_body> req;
        std::string id;
        std::string file_type;
    };

    static const std::unordered_map<std::string, RequesType> changeReqToEnum;

    asio::io_context& ioc_;
    std::string address_;
    std::string port_;
    asio::ssl::context ssl_ctx_;

    asio::thread_pool tp { std::thread::hardware_concurrency() / 2 };
    std::shared_ptr<DataBaseSession> databaseSession;

    Util::ConfigParser config;

    asio::awaitable<void> doSession(ssl_stream stream);
    asio::awaitable<void> listen();

    asio::awaitable<http::response<http::string_body>> requestHandler(http::request<http::string_body> req);
    asio::awaitable<http::response<http::string_body>> analyzesHandler(http::request<http::string_body> req);
    asio::awaitable<http::response<http::string_body>> authGoogleStartHandler(http::request<http::string_body> req);

    asio::awaitable<http::response<http::string_body>> authGoogleCallbackHandler(http::request<http::string_body> req);

    asio::awaitable<http::response<http::string_body>> authMeHandler(http::request<http::string_body> req);

    asio::awaitable<http::response<http::string_body>> authLogoutHandler(http::request<http::string_body> req);
    asio::awaitable<http::response<http::string_body>> handle_document_request(
        std::vector<DocumentRequest> vreq, std::span<Document> cache_docs, asio::any_io_executor cpu_ex);
    asio::awaitable<void> download_extract_store(
        DocumentRequest req, asio::any_io_executor cpu_ex, asio::strand<asio::any_io_executor> store_strand,
        std::shared_ptr<std::vector<Document>> container);
};
}   // namespace Network