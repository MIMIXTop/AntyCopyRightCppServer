#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/http/dynamic_body_fwd.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/message_generator.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <string>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/tcp_stream.hpp>

namespace Network {
class Session {
public:
    Session(boost::asio::io_context& ioc);

    ~Session() = default;

    boost::asio::awaitable<void> connectToSender(const std::string host);
    boost::asio::awaitable<void> stopConnectToSender();

    boost::asio::awaitable<boost::beast::http::response<boost::beast::http::string_body>>
    sendRequest(boost::beast::http::request<boost::beast::http::string_body> req);

    boost::asio::awaitable<boost::beast::http::response<boost::beast::http::string_body>>
    downloadWithRedirect(boost::beast::http::request<boost::beast::http::string_body> req, int maxRedirect = 5);
private:

    boost::asio::ssl::context ctx_ { boost::asio::ssl::context::tlsv12_client };
    boost::asio::ip::tcp::resolver resolver_;
    boost::beast::ssl_stream<boost::beast::tcp_stream> stream_;
    boost::beast::flat_buffer buffer_;
    std::string host_;
    bool is_connected() const;
};
}   // namespace Network