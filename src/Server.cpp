#include "Server.hpp"

#include <grpcpp/create_channel.h>
#include "DocumentReader/DocReader.hpp"
#include "Models/Document.hpp"
#include <boost/json.hpp>

#include <string_view>
#include <iostream>

constexpr std::string_view GOOGLE_CLASSROOM_HOST = "classroom.googleapis.com";
constexpr std::string_view GOOGLE_HOST = "www.googleapis.com";

namespace Network {
Server::Server(
    asio::io_context& io, const std::string& address, const std::string& port,
    const std::string& cert_file, const std::string& private_key_file)
  : ioc_(io)
  , address_(address)
  , port_(port)
  , ssl_ctx_(asio::ssl::context::tlsv12_server) {
    ssl_ctx_.set_options(
        asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2 | asio::ssl::context::no_sslv3 |
        asio::ssl::context::single_dh_use);

    ssl_ctx_.use_certificate_chain_file(cert_file);
    ssl_ctx_.use_private_key_file(private_key_file, asio::ssl::context::pem);
}

void Server::start() { asio::co_spawn(ioc_, listen(), asio::detached); }

const std::unordered_map<std::string, Server::RequesType> Server::changeReqToEnum = {
    { "/api/analyze", GetStudentAnalizis }, { "/api/courses", GetCourseList }
};

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

asio::awaitable<http::response<http::string_body>> Server::requestHandler(http::request<http::string_body> req) {

    if (req.target() != "/api/analyze") {
        co_return http::response<http::string_body>{http::status::bad_request, req.version()};
    }
    std::vector<Document> doc_vec;

    auto json_data = boost::json::parse(req.body());

    for (auto&& item : json_data.at("filesList").as_array()) {
        const auto& file_obj = item.at("file");
        auto file_id = std::string(file_obj.at("file_id").as_string());
        auto file_url = std::string(file_obj.at("file_url").as_string());

        auto session = std::make_shared<Session>(ioc_);

        http::request<http::string_body> g_req{http::verb::get, "/drive/v3/files/" + file_id + "?alt=media", 11};
        g_req.set(http::field::authorization, req[http::field::authorization]);
        g_req.set(http::field::host, GOOGLE_HOST);
        g_req.keep_alive(req.keep_alive());

        auto g_res = co_await session->downloadWithRedirect(g_req);

        auto text = DocReader::zipReader(g_res.body());

        if (!text.has_value()) {
            co_return http::response<http::string_body>{http::status::service_unavailable, req.version()};
        }

        doc_vec.emplace_back(std::move(text.value()), file_id);
    }

    http::request<http::string_body> request{ http::verb::post , "/analysis", req.version() };

    boost::json::array obj_array;

    for (auto&& item : doc_vec) {
        boost::json::value jv = boost::json::value_from(item);
        obj_array.emplace_back(jv);
    }

    request.body() = boost::json::serialize(obj_array);
    request.set(http::field::content_type, "application/json");
    request.set(http::field::host, config["ML_SERVER_HOST"]);
    request.prepare_payload();

    auto session = std::make_shared<Session>(ioc_);
    auto res_message = co_await session->sendRequest<http::string_body>(request);

    http::response<http::string_body> res{http::status::ok, req.version()};
    res.body() = res_message.body();
    co_return res;
}
}   // namespace Network