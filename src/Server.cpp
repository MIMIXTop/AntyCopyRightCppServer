#include "Server.hpp"
#include <boost/url.hpp>
#include <boost/url/urls.hpp>
#include <format>
#include <string>
#include <string_view>
#include <variant>
#include "RequestDTO.hpp"
#include "util.hpp"

namespace Network {

const std::unordered_map<std::string, Server::RequesType> Server::changeReqToEnum = {
    { "/courses", GetCourses },
    { "/courses/courseWork", GetCourseWorks },
    { "/courses/students", GetStudentListOfCourse },
    { "/courses/courseWork/downloadSubmission", DownloadStudentSubmissin },
    { "/courses/courseWork/studentSubmissions", GetStudentSubmissions },
};

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

asio::awaitable<void> Server::streamReader(ssl_stream& stream) {
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
            co_await streamReader(stream);
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

asio::awaitable<http::response<http::dynamic_body>> Server::requestHandler(http::request<http::string_body> req) {
    auto dto = Server::getRequestTypeOfTarget(req.target());

    std::visit(
        util::match {
          [&]([[maybe_unused]] DTOCourseList) {},
          [&](DTOCourseWorksList) {},
          [&](DTOStudentsList) {},
          [&](DTOStudentWorksData) {},
          [&](DTOStudentWorksDownload) {},
          [&](DTOError) {},
        },
        dto);
}

DTOCreateRequest Server::getRequestTypeOfTarget(std::string_view target) {
    boost::url_view uri(target);
    auto it = changeReqToEnum.find(std::string(uri.path()));
    if (it == changeReqToEnum.end()) {
        return DTOError { .errorMessage = "Unknown path" };
    }

    auto pathToken = it->second;

    switch (pathToken) {
        case GetCourses: {
            return DTOCourseList {};
        }
        case GetCourseWorks: {
            if (uri.query().empty()) {
                return DTOError { .errorMessage = "No query parameters found in the request URL." };
            }

            if (!uri.query().contains("courseId=")) {
                return DTOError { .errorMessage = "Missing 'courseId' query parameter." };
            }
            DTOCourseWorksList dto;
            for (auto&& param : uri.params()) {
                if (param.key == "courseId") {
                    dto.courseId = param.value;
                }
            }
            if (dto.courseId.empty()) {
                return DTOError { .errorMessage = "The 'courseId' query parameter is empty." };
            }

            return dto;
        }
        case GetStudentListOfCourse: {
            if (uri.query().empty()) {
                return DTOError { .errorMessage = "No query parameters found in the request URL." };
            }

            if (!uri.query().contains("courseId=")) {
                return DTOError { .errorMessage = "Missing 'courseId' query parameter." };
            }
            DTOStudentsList dto;
            for (auto&& param : uri.params()) {
                if (param.key == "courseId") {
                    dto.courseId = param.value;
                }
            }
            if (dto.courseId.empty()) {
                return DTOError { .errorMessage = "The 'courseId' query parameter is empty." };
            }
            return dto;
        }
        case GetStudentSubmissions: {
            if (uri.query().empty()) {
                return DTOError { .errorMessage = "No query parameters found in the request URL." };
            }

            if (!uri.query().contains("fileId=")) {
                return DTOError { .errorMessage = "Missing 'fileId' query parameter." };
            }
            DTOStudentWorksData dto;

            for (auto&& param : uri.params()) {
                if (param.key == "courseWorkId") {
                    dto.courseWorkId = param.value;
                }

                if (param.key == "courseId") {
                    dto.courseId = param.value;
                }
            }

            if (dto.courseId.empty() || dto.courseWorkId.empty()) {
                return DTOError { .errorMessage = "Missing 'courseId' or 'courseWorkId' query parameter." };
            }
            return dto;
        }
        case DownloadStudentSubmissin: {
            if (uri.query().empty()) {
                return DTOError { .errorMessage = "No query parameters found in the request URL." };
            }

            if (!uri.query().contains("fileId=")) {
                return DTOError { .errorMessage = "Missing 'fileId' query parameter." };
            }

            DTOStudentWorksDownload dto;
            for (auto&& param : uri.params()) {
                if (param.key == "fileId") {
                    dto.fileId = param.value;
                }
            }
            if (dto.fileId.empty()) {
                return DTOError { .errorMessage = "The 'fileId' query parameter is empty." };
            }
            return dto;
        }
        default: {
            return DTOError { .errorMessage = "Unknown request type." };
        }
    }
}

}   // namespace Network