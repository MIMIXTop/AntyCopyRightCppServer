#include "Server.hpp"
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/http/dynamic_body_fwd.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/file_body_fwd.hpp>
#include <boost/beast/http/message_fwd.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/url.hpp>
#include <boost/url/urls.hpp>
#include <format>
#include <string>
#include <string_view>
#include <variant>
#include "RequestDTO.hpp"
#include "util.hpp"

#define GOOGLE_CLASSROOM_HOST "classroom.googleapis.com"
#define GOOGLE_HOST           "www.googleapis.com"

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

    googleSession = std::make_unique<Session>(ioc_);
    classroomSession = std::make_unique<Session>(ioc_);

    asio::co_spawn(ioc_, googleSession->connectToSender(GOOGLE_HOST), asio::detached);
    asio::co_spawn(ioc_, classroomSession->connectToSender(GOOGLE_CLASSROOM_HOST), asio::detached);
}

asio::awaitable<void> Server::streamReader(ssl_stream& stream) {
    beast::flat_buffer buffer;
    http::request<http::string_body> req;

    co_await http::async_read(stream, buffer, req, asio::use_awaitable);

    auto newRequest = co_await requestHandler(req);

    if (newRequest.target() == "/error") {
        http::response<http::string_body> res { http::status::bad_request, req.version() };
        res.set(http::field::server, "AntyCopyRightServer");
        res.set(http::field::content_type, "application/json");
        res.body() = newRequest.body();
        res.prepare_payload();

        co_await http::async_write(stream, res, asio::use_awaitable);
        co_await stream.async_shutdown(asio::use_awaitable);
        co_return;
    } else {
        auto res = co_await sender(newRequest);

        co_await http::async_write(stream, res, asio::use_awaitable);
        co_await stream.async_shutdown(asio::use_awaitable);
        co_return;
    }
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

asio::awaitable<http::request<http::string_body>> Server::requestHandler(http::request<http::string_body> req) {
    auto auth_token_string = req[http::field::authorization];

    auto dto =
        !auth_token_string.empty()
            ? Server::getRequestTypeOfTarget(req.target())
            : DTOError { .errorMessage = "Not found google access token" };

    http::request<http::string_body> googleRequest;
    std::visit(
        util::match {
          [&googleRequest]([[maybe_unused]] DTOCourseList) {
              googleRequest.target("/v1/corses");
              googleRequest.set(http::field::accept, "application/json");
              googleRequest.set(http::field::host, GOOGLE_CLASSROOM_HOST);
          },
          [&googleRequest](DTOCourseWorksList courseWorkList) {
              googleRequest.target(std::format("/v1/courses/{}/courseWork", courseWorkList.courseId));
              googleRequest.set(http::field::accept, "application/json");
              googleRequest.set(http::field::host, GOOGLE_CLASSROOM_HOST);
          },
          [&googleRequest](DTOStudentsList studentList) {
              googleRequest.target(std::format("/v1/courses/{}/students", studentList.courseId));
              googleRequest.set(http::field::accept, "application/json");
              googleRequest.set(http::field::host, GOOGLE_CLASSROOM_HOST);
          },
          [&googleRequest](DTOStudentWorksData studentSubmissins) {
              googleRequest.target(std::format(
                  "/v1/corses/{}/courseWork/{}/StudentSubmissions", studentSubmissins.courseId,
                  studentSubmissins.courseWorkId));
              googleRequest.set(http::field::accept, "application/json");
              googleRequest.set(http::field::host, GOOGLE_CLASSROOM_HOST);
          },
          [&googleRequest](DTOStudentWorksDownload downloadFile) {
              googleRequest.target(std::format("/drive/v3/files/{}?alt=media", downloadFile.fileId));
              googleRequest.set(http::field::host, GOOGLE_HOST);
          },
          [&googleRequest](DTOError error) {
              googleRequest.target("/error");
              googleRequest.body() = std::format(R"({{"error": "{}"}})", error.errorMessage);
              googleRequest.set(http::field::content_type, "application/json");
              googleRequest.set(http::field::host, "localserver");
          },
        },
        dto);
    googleRequest.method(http::verb::get);
    googleRequest.set(http::field::authorization, auth_token_string);
    googleRequest.keep_alive(req.keep_alive());
    googleRequest.prepare_payload();
    co_return googleRequest;
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

asio::awaitable<http::response<http::dynamic_body>> Server::sender(http::request<http::string_body> req) {
    http::response<http::dynamic_body> res;
    if (req[http::field::host] == GOOGLE_HOST) {
        res = co_await googleSession->sendRequest(req);
    } else {
        res = co_await classroomSession->sendRequest(req);
    }
    co_return res;
}
}   // namespace Network