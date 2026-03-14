#include "Server.hpp"
#include <string_view>
#include <iostream>

constexpr std::string_view GOOGLE_CLASSROOM_HOST = "classroom.googleapis.com";
constexpr std::string_view GOOGLE_HOST = "www.googleapis.com";

namespace Network {

const std::unordered_map<std::string, Server::RequesType> Server::changeReqToEnum = {
    { "/api/analyze", GetStudentAnalizis }, { "/api/courses", GetCourseList }
};

Server::Server(
    asio::io_context& io, const std::string& address, const std::string& port, const std::string& cert_file,
    const std::string& private_key_file)
  : ioc_(io)
  , thread_pool_(std::thread::hardware_concurrency())
  , address_(address)
  , port_(port)
  , ssl_ctx_(asio::ssl::context::tlsv12_server) {
    ssl_ctx_.set_options(
        asio::ssl::context::default_workarounds
        | asio::ssl::context::no_sslv2
        | asio::ssl::context::no_sslv3
        | asio::ssl::context::single_dh_use);

    ssl_ctx_.use_certificate_chain_file(cert_file);
    ssl_ctx_.use_private_key_file(private_key_file, asio::ssl::context::pem);
}

void Server::start() {
    asio::co_spawn(ioc_, listen(), asio::detached);
}

asio::awaitable<void> Server::listen() {
    try {
        tcp::acceptor acceptor(ioc_);
        tcp::endpoint endpoint(asio::ip::make_address(address_), std::stoi(port_));

        acceptor.open(endpoint.protocol());
        acceptor.set_option(tcp::acceptor::reuse_address(true));
        acceptor.bind(endpoint);
        acceptor.listen();

        std::println(std::cout, "Server is listening on {}:{}", address_, port_);

        while (true) {
            auto socket = co_await acceptor.async_accept(asio::use_awaitable);
            ssl_stream stream(tcp_stream(std::move(socket)), ssl_ctx_);
            asio::co_spawn(ioc_, doSession(std::move(stream)), asio::detached);
        }
    } catch (const std::exception& e) {
        std::println(std::cerr, "Exception in listen: {}", e.what());
    }
}

asio::awaitable<void> Server::doSession(ssl_stream stream) {
    beast::flat_buffer buffer;

    try {

        beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(10));
        co_await stream.async_handshake(asio::ssl::stream_base::server, asio::use_awaitable);

        while (true) {
            http::request<http::string_body> req;

            beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(30));
            co_await http::async_read(stream, buffer, req, asio::use_awaitable);

            beast::get_lowest_layer(stream).expires_never();

            http::response<http::string_body> res = co_await requestHandler(std::move(req));

            beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(10));
            co_await http::async_write(stream, res, asio::use_awaitable);

            if (res.need_eof()) {
                break;
            }
        }

        beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(5));
        co_await stream.async_handshake(asio::ssl::stream_base::server);

    } catch (const boost::system::system_error& e) {
        if (e.code() != http::error::end_of_stream && e.code() != asio::error::eof) {
            std::println(std::cerr, "Session error: {}", e.what());
        }
    }
}

asio::awaitable<http::response<http::string_body>> Server::requestHandler(http::request<http::string_body> req) {
    co_return http::response<http::string_body>();
}

}   // namespace Network