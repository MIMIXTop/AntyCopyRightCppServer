#include "Server.hpp"
#include <boost/asio/async_result.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/error.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/flat_stream.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/impl/read.hpp>
#include <boost/beast/http/impl/write.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <string>
namespace Network {
Server::Server(
    asio::io_context& ioc, const std::string& address, const std::string& port, const std::string& cert_file,
    const std::string& private_key_file)
  : ioc_(ioc), address_(address), port_(port), ssl_ctx_(asio::ssl::context::tlsv12) {
    ssl_ctx_.set_options(
        asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv3 | asio::ssl::context::no_sslv3 |
        asio::ssl::context::single_dh_use);
    ssl_ctx_.use_certificate_chain_file(cert_file);
    ssl_ctx_.use_private_key_file(private_key_file, asio::ssl::context::pem);
}

asio::awaitable<void> Server::handelRequest(ssl_stream& stream) {
    beast::flat_buffer buffer;
    http::request<http::string_body> req;

    co_await http::async_read(stream, buffer, req, asio::use_awaitable);

    http::response<http::string_body> res { http::status::ok, req.version() };
    res.set(http::field::server, "AntyCopyRightServer");
    res.set(http::field::content_type, "application/json");
    res.body() = R"({"status": "ok", "message": "Hello from C++20 HTTPS server!"})";
    res.prepare_payload();

    co_await http::async_write(stream, res, asio::use_awaitable);
    co_await stream.async_shutdown(asio::use_awaitable);
}

asio::awaitable<void> Server::doSession(ssl_stream stream) {
    try {
        co_await stream.async_handshake(asio::ssl::stream_base::server, asio::use_awaitable);
        while (true) {
            co_await handelRequest(stream);
        }
    } catch (...) {
        beast::error_code ec;
        stream.next_layer().socket().shutdown(tcp::socket::shutdown_both, ec);
    }
}

asio::awaitable<void> Server::listen() {
    tcp::acceptor acceptor(ioc_);
    tcp::endpoint endpoint(asio::ip::make_address(address_), std::stoi(port_));

    acceptor.open(endpoint.protocol());
    acceptor.set_option(asio::socket_base::reuse_address(true));
    acceptor.bind(endpoint);
    acceptor.listen();

    while (true) {
        auto socket = co_await acceptor.async_accept(asio::use_awaitable);
        ssl_stream stream(std::move(socket), ssl_ctx_);
        asio::co_spawn(ioc_, doSession(std::move(stream)), asio::detached);
    }
}

void Server::start() { asio::co_spawn(ioc_, listen(), asio::detached); }

}   // namespace Network