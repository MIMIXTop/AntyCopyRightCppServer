#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <string>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/http/vector_body.hpp>

namespace Network {
class SslSession {
public:
    explicit SslSession(boost::asio::io_context& ioc);

    ~SslSession() = default;

    boost::asio::awaitable<void> connectToSender(const std::string host, const std::string port);
    boost::asio::awaitable<void> stopConnectToSender();

    template<typename T>
    boost::asio::awaitable<boost::beast::http::response<T>>
    sendRequest(boost::beast::http::request<boost::beast::http::string_body> req);

    boost::asio::awaitable<boost::beast::http::response<boost::beast::http::vector_body<unsigned char>>>
    downloadWithRedirect(boost::beast::http::request<boost::beast::http::string_body> req, int maxRedirect = 5);
private:

    boost::asio::ssl::context ctx_ { boost::asio::ssl::context::tlsv12_client };
    boost::asio::ip::tcp::resolver resolver_;
    boost::beast::ssl_stream<boost::beast::tcp_stream> stream_;
    boost::beast::flat_buffer buffer_;
    std::string port_;
    std::string host_;
    bool is_connected() const;
};
}   // namespace Network