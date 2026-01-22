#pragma once

#include "RequestDTO.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/asio/ssl.hpp>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Network {
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;
using ssl_stream = asio::ssl::stream<beast::tcp_stream>;

class Server {
public:
    Server(asio::io_context& io, const std::string& address, const std::string& port, const std::string& cert_file,
           const std::string& private_key_file);

    void start();

private:
    enum RequesType {
        GetCourses,
        GetCourseWorks,
        GetStudentListOfCourse,
        GetStudentSubmissions,
        DownloadStudentSubmissin
    };

    static const std::unordered_map<std::string, RequesType> changeReqToEnum;

    asio::io_context& ioc_;
    std::string address_;
    std::string port_;
    asio::ssl::context ssl_ctx_;

    asio::awaitable<void> streamReader(ssl_stream& stream);
    asio::awaitable<void> doSession(ssl_stream stream);
    asio::awaitable<http::response<http::dynamic_body>> requestHandler(http::request<http::string_body> req);
    DTOCreateRequest getRequestTypeOfTarget(std::string_view target);
    asio::awaitable<void> listen();
};
}   // namespace Network