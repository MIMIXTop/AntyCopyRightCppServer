//
// Created by mimixtop on 26.12.2025.
//

#include "Session.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/http/empty_body.hpp>
#include <boost/beast/http/string_body_fwd.hpp>
#include <openssl/err.h>
#include <boost/stacktrace.hpp>
#include <iostream>
#include <print>
#include <boost/url/parse.hpp>
#include <boost/url/url.hpp>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

using tcp = asio::ip::tcp;

namespace Network {
Session::Session(asio::io_context& ioc) : resolver_(ioc), stream_(ioc, ctx_) {
    ctx_.set_default_verify_paths();
    ctx_.set_verify_mode(asio::ssl::context::verify_peer);
}

bool Session::is_connected() const { return beast::get_lowest_layer(stream_).socket().is_open(); }

asio::awaitable<void> Session::connectToSender(const std::string host) {
    try {
        if (is_connected() && host == host_) {
            co_return;
        }

        if (is_connected()) {
            co_await stopConnectToSender();
        }

        std::println("Connecting to host: {}", host);
        host_ = host;

        auto result = co_await resolver_.async_resolve(host, "443", asio::use_awaitable);

        co_await beast::get_lowest_layer(stream_).async_connect(result, asio::use_awaitable);

        if (!SSL_set_tlsext_host_name(stream_.native_handle(), host_.c_str())) {
            throw boost::system::system_error(
                boost::system::error_code(ERR_get_error(), asio::error::get_ssl_category()));
        }

        beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));
        co_await stream_.async_handshake(asio::ssl::stream_base::client, asio::use_awaitable);

        beast::get_lowest_layer(stream_).expires_never();
    } catch (std::exception& e) {
        std::println(std::cerr, "Exception in connectToSender: {}", e.what());
        throw;
    }
}

asio::awaitable<void> Session::stopConnectToSender() {
    try {
        if (is_connected()) {
            beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(5));

            boost::system::error_code ec;
            co_await stream_.async_shutdown(asio::redirect_error(asio::use_awaitable, ec));

            if (ec && ec != asio::error::eof && ec != asio::ssl::error::stream_truncated) {
                std::println(std::cerr, "Shutdown error: {}", ec.message());
            }

            beast::get_lowest_layer(stream_).socket().close(ec);
            beast::get_lowest_layer(stream_).expires_never();
        }

    } catch (std::exception& e) {
        std::println(std::cerr, "Exception in stopConnectToSender: {}", e.what());
    }
}

asio::awaitable<http::response<http::string_body>> Session::sendRequest(http::request<http::string_body> req) {
    try {
        auto targetHost = std::string(req[http::field::host]);
        co_await connectToSender(targetHost);

        beast::get_lowest_layer(stream_).expires_after(std::chrono::seconds(30));
        co_await http::async_write(stream_, req, asio::use_awaitable);

        http::response<http::string_body> res;

        beast::get_lowest_layer(stream_).expires_after(std::chrono::minutes(5));

        buffer_.consume(buffer_.size());
        co_await http::async_read(stream_, buffer_, res, asio::use_awaitable);

        beast::get_lowest_layer(stream_).expires_never();
        co_return res;
    } catch (const boost::system::system_error& se) {
        std::println(std::cerr, "Network error in sendRequest: {}", se.what());
        throw;
    }
}

asio::awaitable<http::response<http::string_body>>
Session::downloadWithRedirect(http::request<http::string_body> req, int maxRedirect) {
    int redirectCount = 0;

    while (redirectCount < maxRedirect) {
        auto res = co_await sendRequest(req);

        if (res.result() == http::status::found || res.result() == http::status::moved_permanently ||
            res.result() == http::status::temporary_redirect) {
            auto loc_it = res.find(http::field::location);

            if (loc_it == res.end()) {
                throw std::runtime_error("Redirect code received but no Location header found");
            }

            std::string location_url = std::string(loc_it->value());
            std::println("Redirecting to: {}", location_url);

            auto result = boost::urls::parse_uri_reference(location_url);
            if (!result) {
                throw std::runtime_error("Invalid redirect URL format" + location_url);
            }

            boost::urls::url_view parsedUrl = result.value();

            std::string newHost, newTarget;

            if (!parsedUrl.host().empty()) {
                newHost = parsedUrl.host();
            } else {
                newHost = std::string(req[http::field::host]);
            }

            newTarget = parsedUrl.encoded_path();
            if (newTarget.empty()) {
                newTarget = "/";
            }


            if (parsedUrl.has_query()) {
                newTarget += "?";
                newTarget += parsedUrl.encoded_query();
            }

            if (newHost != std::string(req[http::field::host])) {
                co_await stopConnectToSender();
            }

            http::request<http::string_body> newReq {http::verb::get, newTarget, 11};
            newReq.set(http::field::host, newHost);

            if (req.find(http::field::user_agent) != req.end()) {
                newReq.set(http::field::user_agent, req[http::field::user_agent]);
            }
            if (req.find(http::field::authorization) != req.end()) {
                newReq.set(http::field::authorization, req[http::field::authorization]);
            }

            req = std::move(newReq);
            redirectCount++;
        } else {
            co_return res;
        }
    }
    throw std::runtime_error("Too many redirects");
}

}   // namespace Network