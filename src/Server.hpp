#pragma once

#include "Auth/GoogleTokenManager.hpp"
#include "Models/Paragraph.hpp"
#include "Models/Document.hpp"
#include "Session/DataBaseSession.hpp"
#include "Session/SslSession.hpp"
#include "Util/ConfigParser.hpp"

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http.hpp>
#include <string>
#include <unordered_map>
#include <boost/url/error_types.hpp>
#include <boost/url/url_view.hpp>

namespace Network {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;
using tcp_stream = beast::tcp_stream;

class Server {
public:
    Server(asio::io_context& io, const std::string& address, const std::string& port);

    void start();

private:
    enum RequesType {
        GetStudentAnalizis,
        AuthGoogleStart,
        AuthGoogleCallback,
        AuthMe,
        AuthLogout
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

    asio::thread_pool tp { std::thread::hardware_concurrency() / 2 };
    std::shared_ptr<DataBaseSession> databaseSession;

    Util::ConfigParser config;

    asio::awaitable<void> doSession(tcp_stream stream);
    asio::awaitable<void> listen();
    void applyCorsHeaders(http::response<http::string_body>& res) const;

    asio::awaitable<http::response<http::string_body>> requestHandler(http::request<http::string_body> req);
    asio::awaitable<http::response<http::string_body>> analyzesHandler(http::request<http::string_body> req);

    asio::awaitable<http::response<http::string_body>> authGoogleStartHandler(http::request<http::string_body> req);

    asio::awaitable<http::response<http::string_body>> authGoogleCallbackHandler(http::request<http::string_body> req);

    asio::awaitable<http::response<http::string_body>> authMeHandler(http::request<http::string_body> req);

    asio::awaitable<http::response<http::string_body>> authLogoutHandler(http::request<http::string_body> req);
    asio::awaitable<http::response<http::string_body>> classroomProxyHandler(http::request<http::string_body> req, boost::urls::url_view target);
    asio::awaitable<http::response<http::string_body>> handle_document_request(
        std::vector<DocumentRequest> vreq, std::span<Document> cache_docs, asio::any_io_executor cpu_ex);
    asio::awaitable<void> download_extract_store(
        DocumentRequest req, asio::any_io_executor cpu_ex, asio::strand<asio::any_io_executor> store_strand,
        std::shared_ptr<std::vector<Document>> container);

    template<typename T>
    std::optional<std::string> getCookie(const http::request<T>& req, std::string_view cookieName);

    asio::awaitable<std::tuple<std::optional<AppSession>, std::string>> getSessionFromCookie(http::request<http::string_body>& req);

};
}   // namespace Network
