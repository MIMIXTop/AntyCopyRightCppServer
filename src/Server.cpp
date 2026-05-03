#include "Server.hpp"

#include "DocumentReader/DocReader.hpp"
#include "Models/Document.hpp"
#include "Session/SimpleSession.hpp"

#include <boost/json.hpp>

#include <boost/asio/experimental/parallel_group.hpp>

#include <execution>
#include <filesystem>
#include <string_view>
#include <iostream>

constexpr std::string_view GOOGLE_CLASSROOM_HOST = "classroom.googleapis.com";
constexpr std::string_view GOOGLE_HOST = "www.googleapis.com";

namespace X = boost::asio::experimental;

namespace Network {
Server::Server(
    asio::io_context& io, const std::string& address, const std::string& port, const std::string& cert_file,
    const std::string& private_key_file)
  : ioc_(io), address_(address), port_(port), ssl_ctx_(asio::ssl::context::tlsv12_server) {
    ssl_ctx_.set_options(
        asio::ssl::context::default_workarounds | asio::ssl::context::no_sslv2 | asio::ssl::context::no_sslv3 |
        asio::ssl::context::single_dh_use);

    ssl_ctx_.use_certificate_chain_file(cert_file);
    ssl_ctx_.use_private_key_file(private_key_file, asio::ssl::context::pem);
}

void Server::start() { asio::co_spawn(ioc_, listen(), asio::detached); }

const std::unordered_map<std::string, Server::RequesType> Server::changeReqToEnum = {
    { "/api/analyze", GetStudentAnalizis }, { "/api/getRefreshToken", GetRefreshToken }
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
            res.set(http::field::access_control_allow_origin, "*");
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
    if (req.method() == http::verb::options) {
        http::response<http::string_body> res { http::status::no_content, req.version() };
        res.set(http::field::access_control_allow_methods, "POST, GET, OPTIONS");
        res.set(http::field::access_control_allow_headers, "Content-Type, Authorization");
        co_return res;
    }

    std::string target = req.target();

    auto it = changeReqToEnum.find(target);
    if (it == changeReqToEnum.end()) {
        co_return http::response<http::string_body> { http::status::not_found, req.version() };
    }

    switch (auto&& [_, value] = *it; value) {
        case GetStudentAnalizis:
            if (req.method() == http::verb::post) {
                co_return co_await analyzesHandler(req);
            }
            co_return http::response<http::string_body> { http::status::bad_request, req.version() };
        case GetRefreshToken:
            co_return http::response<http::string_body> { http::status::service_unavailable, req.version() };
    }
}

asio::awaitable<http::response<http::string_body>> Server::analyzesHandler(http::request<http::string_body> req) {
    std::vector<Document> doc_vec;

    auto json_data = boost::json::parse(req.body());

    std::vector<DocumentRequest> req_vec;

    for (auto&& item : json_data.at("filesList").as_array()) {
        const auto& file_obj = item.at("file");
        auto file_id = std::string(file_obj.at("file_id").as_string());
        auto file_url = std::string(file_obj.at("file_url").as_string());
        auto file_type = std::string(file_obj.at("file_type").as_string());

        auto session = std::make_shared<SslSession>(ioc_);

        //auto auth_header = req[http::field::authorization];
        http::request<http::string_body> g_req { http::verb::get, "/drive/v3/files/" + file_id + "?alt=media", 11 };
        g_req.set(http::field::authorization, req[http::field::authorization]);
        g_req.set(http::field::host, GOOGLE_HOST);
        g_req.keep_alive(req.keep_alive());

        req_vec.push_back({ .req = g_req, .id = file_id, .file_type = file_type });
    }

    auto res = co_await handle_document_request(req_vec, tp.get_executor());
    co_return res;
}

asio::awaitable<http::response<http::string_body>>
Server::getRefreshTokenHandler(http::request<http::string_body> req) {}


asio::awaitable<http::response<http::string_body>>
Server::handle_document_request(std::vector<DocumentRequest> vreq, asio::any_io_executor cpu_ex) {
    auto net_ex = co_await asio::this_coro::executor;

    auto container = std::make_shared<std::vector<Document>>();

    auto stor_strand = asio::make_strand(asio::any_io_executor(net_ex));

    auto make_op = [&](DocumentRequest document_request) {
        return asio::co_spawn(
            net_ex, download_extract_store(std::move(document_request), cpu_ex, stor_strand, container),
            asio::deferred);
    };

    auto first = make_op(std::move(vreq.front()));

    using Op = decltype(first);

    std::vector<Op> op_vec;

    op_vec.reserve(vreq.size());
    op_vec.emplace_back(std::move(first));

    for (std::size_t i = 1; i < vreq.size(); ++i) {
        op_vec.emplace_back(make_op(std::move(vreq[i])));
    }

    auto group = X::make_parallel_group(std::move(op_vec));

    auto [order, errors] = co_await std::move(group).async_wait(X::wait_for_all(), asio::use_awaitable);

    for (const auto & i : errors) {
        if (i) {
            std::rethrow_exception(i);
        }
    }

    http::request<http::string_body> request { http::verb::post, "/analysis", 11 };

    boost::json::array obj_array;

    for (auto&& item : *container) {
        boost::json::value jv = boost::json::value_from(item);
        obj_array.emplace_back(jv);
    }

    request.body() = boost::json::serialize(obj_array);
    request.set(http::field::content_type, "application/json");
    request.set(http::field::host, config["ML_SERVER_HOST"]);
    request.prepare_payload();

    auto session = std::make_shared<SimpleSession>(ioc_);
    auto res_message = co_await session->sendRequest<http::string_body>(request);

    http::response<http::string_body> res { http::status::ok, 11 };
    res.body() = res_message.body();
    res.set(http::field::access_control_allow_origin, "*");
    co_return res;
}

asio::awaitable<void> Server::download_extract_store(
    DocumentRequest req, asio::any_io_executor cpu_ex, asio::strand<asio::any_io_executor> store_strand,
    std::shared_ptr<std::vector<Document>> container) {
    auto download_session = std::make_shared<SslSession>(ioc_);
    auto doc_req = co_await download_session->downloadWithRedirect(req.req);

    std::println(std::cout, "Попытка скачать файл {}.", req.id);

    if (doc_req.result() != http::status::ok) {
        std::string temp = std::string(doc_req.body().begin(), doc_req.body().end());

        std::println(
            std::cerr, "ОШИБКА СКАЧИВАНИЯ {}: Код {}. Тело: {}", req.id, static_cast<unsigned>(doc_req.result()),
            std::string(doc_req.body().begin(), doc_req.body().end()));
    }

    co_await asio::post(cpu_ex, asio::use_awaitable);

    auto doc_text = DocReader::DocumentReaderFromRaw(doc_req.body(), req.file_type);

    co_await asio::post(store_strand, asio::use_awaitable);

    container->emplace_back(std::move(doc_text.value()), req.id);
}
}   // namespace Network