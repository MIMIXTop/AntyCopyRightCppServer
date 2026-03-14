#pragma once

#include "Session.hpp"
#include "ConfigParser.hpp"

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/beast.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/http/dynamic_body_fwd.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <memory>
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
        GetCourseList,
    };

    static const std::unordered_map<std::string, RequesType> changeReqToEnum;

    asio::io_context& ioc_;
    std::string address_;
    std::string port_;
    asio::ssl::context ssl_ctx_;
    asio::thread_pool thread_pool_;

    Util::ConfigParser config;

    asio::awaitable<void> doSession(ssl_stream stream);
    asio::awaitable<void> listen();
    asio::awaitable<http::response<http::string_body>> requestHandler(http::request<http::string_body> req);

};
}   // namespace Network