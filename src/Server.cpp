#include "Server.hpp"

#include "Auth/GoogleOAuthClient.hpp"
#include "DocumentReader/DocReader.hpp"
#include "Models/Document.hpp"
#include "Session/SimpleSession.hpp"
#include "Util/Encrypt.hpp"
#include "Util/TimeFunc.hpp"

#include <boost/json.hpp>
#include <boost/url.hpp>
#include <boost/url/params_ref.hpp>

#include <boost/asio/experimental/parallel_group.hpp>

#include <execution>
#include <filesystem>
#include <string_view>
#include <iostream>
#include <ranges>
#include <string>

namespace {
    std::array<std::string_view, 8> googleOAuthScopes {
        "https://www.googleapis.com/auth/classroom.courses.readonly",
        "https://www.googleapis.com/auth/classroom.rosters.readonly",
        "https://www.googleapis.com/auth/classroom.profile.emails",
        "https://www.googleapis.com/auth/classroom.coursework.students.readonly",
        "https://www.googleapis.com/auth/drive.readonly",
        "openid",
        "email",
        "profile"
    } ;

}

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
    databaseSession = std::make_shared<DataBaseSession>();
}

void Server::start() { asio::co_spawn(ioc_, listen(), asio::detached); }

const std::unordered_map<std::string, Server::RequesType> Server::changeReqToEnum = {
    { "/api/analyze", GetStudentAnalizis },
    { "/api/getRefreshToken", GetRefreshToken },
    { "/api/auth/google/start", AuthGoogleStart },
    { "/api/auth/google/callback", AuthGoogleCallback },
    { "/api/auth/me", AuthMe },
    { "/api/auth/logout", AuthLogout },
};

template <typename T>
std::optional<std::string> Server::getCookie(const http::request<T>& req, std::string_view cookieName) {

    for (auto&& [key, value] : http::param_list(req[http::field::cookie])) {
        if (key == cookieName) {
            return  value;
        }
    }

    return std::nullopt;
}

void Server::applyCorsHeaders(http::response<http::string_body>& res) const {
    const auto appOrigin = config["APP_ORIGIN"];
    if (!appOrigin.empty()) {
        res.set(http::field::access_control_allow_origin, appOrigin);
    }
    res.set("Access-Control-Allow-Credentials", "true");
    res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
    res.set(http::field::access_control_allow_headers, "Content-Type, Authorization");
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
            applyCorsHeaders(res);
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
        co_return res;
    }

    auto parsedTarget = boost::urls::parse_origin_form(req.target());

    if (!parsedTarget) {
        co_return http::response<http::string_body>{http::status::bad_request, req.version()};
    }

    std::string target = std::string(parsedTarget.value().path());

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
        case AuthGoogleStart:
            if (req.method() == http::verb::get) {
                co_return co_await authGoogleStartHandler(req);
            }
            co_return http::response<http::string_body> { http::status::method_not_allowed, req.version() };
        case AuthGoogleCallback:
            if (req.method() == http::verb::get) {
                co_return co_await authGoogleCallbackHandler(req);
            }
            co_return http::response<http::string_body> { http::status::method_not_allowed, req.version() };
        case AuthLogout:
            if (req.method() == http::verb::post) {
                co_return co_await authLogoutHandler(req);
            }
            co_return http::response<http::string_body> { http::status::method_not_allowed, req.version() };
        case AuthMe:
            if (req.method() == http::verb::get) {
                co_return co_await authMeHandler(req);
            }
            co_return http::response<http::string_body> { http::status::method_not_allowed, req.version() };
    }
}

asio::awaitable<http::response<http::string_body>> Server::analyzesHandler(http::request<http::string_body> req) {
    std::vector<Document> doc_vec;

    auto json_data = boost::json::parse(req.body());

    std::vector<DocumentRequest> req_vec;

    for (auto&& item : json_data.at("filesList").as_array()) {
        const auto& file_obj = item.at("file");
        auto file_id = std::string(file_obj.at("file_id").as_string());

        auto&& result = co_await databaseSession->selectDocumentById(file_id);
        if (result.has_value()) {
            doc_vec.push_back(result.value());
            continue;
        }

        auto file_url = std::string(file_obj.at("file_url").as_string());
        auto file_type = std::string(file_obj.at("file_type").as_string());
        http::request<http::string_body> g_req { http::verb::get, "/drive/v3/files/" + file_id + "?alt=media", 11 };
        g_req.set(http::field::authorization, req[http::field::authorization]);
        g_req.set(http::field::host, GOOGLE_HOST);
        g_req.keep_alive(req.keep_alive());

        req_vec.push_back({ .req = g_req, .id = file_id, .file_type = file_type });
    }

    auto res = co_await handle_document_request(req_vec, doc_vec, tp.get_executor());
    co_return res;
}
asio::awaitable<http::response<http::string_body>>
Server::authGoogleStartHandler(http::request<http::string_body> req) {
    using namespace std::literals;

    if (config["GOOGLE_CLIENT_ID"].empty() || config["GOOGLE_REDIRECT_URI"].empty()) {
        http::response<http::string_body> res{http::status::internal_server_error, req.version()};
        res.body() = "Google OAuth config is missing";
        res.prepare_payload();
        co_return res;
    }

    auto randomToken = util::randomUrlSafeToken();
    auto randomTokenHash = util::sha256Hex(randomToken);
    auto expiresAt = util::time::getCurrentTimeAfterMinutes(5);

    if (auto stateResult = co_await databaseSession->insertOAuthState(randomTokenHash, expiresAt); !stateResult) {
        http::response<http::string_body> res{http::status::internal_server_error, req.version()};
        res.body() = "Failed to create OAuth state";
        res.prepare_payload();
        co_return res;
    }

    auto scope = googleOAuthScopes | std::views::join_with(" "sv) | std::ranges::to<std::string>();

    std::string_view baseUrl = "https://accounts.google.com/o/oauth2/v2/auth";
    boost::urls::url redirectUrl{baseUrl};

    redirectUrl.params().append({
        {"client_id", config["GOOGLE_CLIENT_ID"]},
        {"redirect_uri", config["GOOGLE_REDIRECT_URI"]},
        {"response_type", "code"},
        {"scope", scope},
        {"access_type", "offline"},
        {"include_granted_scopes", "true"},
        {"state", randomToken}
    });

    http::response<http::string_body> res{http::status::found, req.version()};
    res.set(http::field::location, redirectUrl.buffer());
    res.prepare_payload();

    co_return res;
}
asio::awaitable<http::response<http::string_body>>
Server::authGoogleCallbackHandler(http::request<http::string_body> req) {

    auto url = boost::urls::parse_origin_form(req.target());

    if (!url) {
        co_return http::response<http::string_body>{http::status::bad_request, req.version()};
    }

    if (auto error = url->params().find("error"); error != url->params().end()) {
        http::response<http::string_body> res{http::status::unauthorized, req.version()};
        res.body() = std::string((*error).value);
        res.prepare_payload();
        co_return res;
    }

    auto code = url->params().find("code");
    auto state = url->params().find("state");

    if (code == url->params().end() || state == url->params().end()) {
        http::response<http::string_body> res{http::status::bad_request, req.version()};
        res.prepare_payload();
        co_return res;
    }

    auto stateValue = (*state).value;

    auto stateHash = util::sha256Hex(stateValue);
    auto now = util::time::getCurrentTimestamp();

    if (auto ok = co_await databaseSession->consumeOAuthState(stateHash, now); ok == false) {
        co_return http::response<http::string_body> {http::status::unauthorized, req.version()};
    }

    GoogleUserInfo client;
    GoogleTokenResponse token;
    try {
        GoogleOAuthClient googleOAuthClient{ioc_.get_executor()};
        token = co_await googleOAuthClient.exchangeCodeForTokens((*code).value);
        client = co_await googleOAuthClient.fetchUserInfo(token.accessToken);
    } catch (const std::exception& e) {
        std::println(std::cerr, "Google OAuth callback error: {}", e.what());
        http::response<http::string_body> res{http::status::bad_gateway, req.version()};
        res.body() = "Google OAuth request failed";
        res.prepare_payload();
        co_return res;
    }

    auto user = co_await databaseSession->selectAuthUserByGoogleSub(client.sub);
    std::optional<AuthUser> authUser;
    auto loginAt = util::time::getCurrentTimestamp();
    if (!user.has_value()) {
        authUser = co_await databaseSession->insertAuthUser(client.sub, client.email, client.name, client.pictureUrl, loginAt);
    } else {
        authUser = co_await databaseSession->updateAuthUserLogin(user->id, client.email, client.name, client.pictureUrl, loginAt);
    }

    if (!authUser.has_value()) {
        co_return http::response<http::string_body> {http::status::internal_server_error, req.version()};;
    }

    auto tokenEncryptionKey = std::string(config["TOKEN_ENCRYPTION_KEY"]);
    if (tokenEncryptionKey.empty()) {
        tokenEncryptionKey = std::string(config["SECRET_KEY"]);
    }
    if (tokenEncryptionKey.empty()) {
        http::response<http::string_body> res{http::status::internal_server_error, req.version()};
        res.body() = "Token encryption key is missing";
        res.prepare_payload();
        co_return res;
    }

    auto access_token_enc = util::textEncrypt(token.accessToken, tokenEncryptionKey);
    std::optional<std::string> refresh_token_enc;
    if (token.refreshToken.has_value()) {
        refresh_token_enc = util::textEncrypt(token.refreshToken.value(), tokenEncryptionKey);
    } else {
        refresh_token_enc = std::nullopt;
    }

    auto expiresAt = util::time::getCurrentTimeAfterSeconds(token.expiresIn);
    auto res = co_await databaseSession->upsertGoogleOAuthTokens({
        .userId = authUser->id,
        .accessTokenEnc = access_token_enc,
        .refreshTokenEnc = refresh_token_enc,
        .expiresAt = expiresAt,
        .scope = token.scope,
        .tokenType = token.tokenType
    });

    if (!res) {
        co_return http::response<http::string_body> {http::status::internal_server_error, req.version()};;
    }

    auto sessionId = util::randomUrlSafeToken();
    auto sessionHash = util::sha256Hex(sessionId);
    auto sessionTtlDaysText = std::string(config["SESSION_TTL_DAYS"]);
    if (sessionTtlDaysText.empty()) {
        sessionTtlDaysText = "7";
    }
    auto sessionTtlDays = std::stoi(sessionTtlDaysText);
    auto sessionMaxAge = sessionTtlDays * 24 * 60 * 60;
    auto sessionExpiresAt = util::time::getCurrentTimeAfterMinutes(sessionTtlDays * 24 * 60);
    std::string userAgent;

    if (auto it = req.find(http::field::user_agent); it != req.end()) {
        userAgent = std::string(it->value());
    }
    auto resAppSession = co_await databaseSession->insertAppSession(
        "",
        authUser->id,
        sessionHash,
        sessionExpiresAt,
        userAgent
    );

    if (!resAppSession) {
        co_return http::response<http::string_body> {http::status::internal_server_error, req.version()};;
    }
    http::response<http::string_body> ress{http::status::found, req.version()};
    ress.set(http::field::location, config["APP_ORIGIN"]);
    ress.set(http::field::server, "AntyCopyRightCppServer");
    auto cookieName = std::string(config["SESSION_COOKIE_NAME"]);
    if (cookieName.empty()) {
        cookieName = "anty_session";
    }
    auto cookie_value = std::format(
        "{}={}; HttpOnly; Secure; SameSite=Lax; Path=/; Max-Age={}",
        cookieName,
        sessionId,
        sessionMaxAge
    );
    ress.set(http::field::set_cookie, cookie_value);
    ress.prepare_payload();
    co_return ress;
}
asio::awaitable<http::response<http::string_body>> Server::authMeHandler(http::request<http::string_body> req) {
    std::string_view cookieName = config["SESSION_COOKIE_NAME"];
    if (cookieName.empty()) {
        cookieName = "anty_session";
    }

    auto sessionId = getCookie(req, cookieName);
    if (sessionId == std::nullopt) {
        co_return http::response<http::string_body> {http::status::unauthorized, req.version()};
    }

    auto sessionHash = util::sha256Hex(sessionId.value());
    auto now = util::time::getCurrentTimestamp();

    auto session = co_await databaseSession->selectActiveAppSession(sessionHash, now);

    if (session == std::nullopt) {
        co_return http::response<http::string_body> {http::status::unauthorized, req.version()};
    }

    auto sessionUpdate = co_await databaseSession->updateAppSessionLastSeen(sessionHash, now);

    if (!sessionUpdate) {
        std::println(std::cerr, "Failed to update app session last_seen_at");
    }

    auto user = co_await databaseSession->selectAuthUserById(session->userId);
    if (user == std::nullopt) {
        co_return http::response<http::string_body> {http::status::unauthorized, req.version()};
    }

    boost::json::object json;
    boost::json::object userJson;
    userJson["id"] = user->id;
    userJson["name"] = user->name;
    userJson["email"] = user->email;
    userJson["googleSub"] = user->googleSub;
    userJson["picture"] = user->pictureUrl;

    json["user"] = userJson;

    http::response<http::string_body> res{http::status::ok, req.version()};
    res.body() = boost::json::serialize(json);
    res.set(http::field::content_type, "application/json");
    res.set(http::field::cache_control, "no-store");
    res.prepare_payload();
    co_return res;
}
asio::awaitable<http::response<http::string_body>> Server::authLogoutHandler(http::request<http::string_body> req) {

    std::string_view cookieName = config["SESSION_COOKIE_NAME"];
    if (cookieName.empty()) {
        cookieName = "anty_session";
    }

    auto sessionId = getCookie(req, cookieName);
    if (sessionId == std::nullopt) {
        co_return http::response<http::string_body> {http::status::no_content, req.version()};
    }

    auto sessionHash = util::sha256Hex(sessionId.value());
    auto now = util::time::getCurrentTimestamp();

    co_await databaseSession->revokeAppSession(sessionHash, now);

    http::response<http::string_body> res{http::status::no_content, req.version()};
    auto cookie_value = std::format(
        "{}=; HttpOnly; Secure; SameSite=Lax; Path=/; Max-Age=0",
        cookieName
    );

    res.set(http::field::set_cookie,cookie_value);
    res.prepare_payload();
    co_return res;
}

asio::awaitable<http::response<http::string_body>> Server::handle_document_request(
    std::vector<DocumentRequest> vreq, std::span<Document> cache_docs, asio::any_io_executor cpu_ex) {
    auto container = std::make_shared<std::vector<Document>>();

    if (!vreq.empty()) {
        auto net_ex = co_await asio::this_coro::executor;

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

        for (const auto& i : errors) {
            if (i) {
                std::rethrow_exception(i);
            }
        }

        for (auto&& doc : *container) {
            co_await databaseSession->insertDocument(doc);
        }
    }

    http::request<http::string_body> request { http::verb::post, "/analysis", 11 };

    boost::json::array obj_array;

    if (!cache_docs.empty()) {
        container->insert_range(container->end(), cache_docs);
    }

    for (auto&& item : *container) {
        boost::json::value jv = boost::json::value_from(item);
        obj_array.emplace_back(jv);
    }

    request.body() = boost::json::serialize(obj_array);
    request.set(http::field::content_type, "application/json");
    request.set(http::field::host, config["ML_SERVER_HOST"]);
    request.prepare_payload();

    auto session = std::make_shared<SimpleSession>(ioc_.get_executor());
    auto res_message = co_await session->sendRequest<http::string_body>(request);

    http::response<http::string_body> res { http::status::ok, 11 };
    res.body() = res_message.body();
    co_return res;
}

asio::awaitable<void> Server::download_extract_store(
    DocumentRequest req, asio::any_io_executor cpu_ex, asio::strand<asio::any_io_executor> store_strand,
    std::shared_ptr<std::vector<Document>> container) {
    auto download_session = std::make_shared<SslSession>(ioc_.get_executor());
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
