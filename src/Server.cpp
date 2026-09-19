#include "Server.hpp"

#include "Services/Auth/GoogleOAuthClient.hpp"
#include "DocumentReader/DocReader.hpp"
#include "Models/Document.hpp"
#include "Session/SimpleSession.hpp"
#include "Util/Encrypt.hpp"
#include "Util/NetworkHealper.hpp"
#include "Util/TimeFunc.hpp"

#include <boost/json.hpp>
#include <boost/url.hpp>
#include <boost/url/params_ref.hpp>

#include <boost/asio/experimental/parallel_group.hpp>

#include <algorithm>
#include <execution>
#include <filesystem>
#include <iterator>
#include <string_view>
#include <iostream>
#include <print>
#include <ranges>
#include <string>
#include <map>

#include "Services/Auth/GoogleTokenManager.hpp"
#include "Util/AsyncExecution.hpp"

namespace {
    std::array<std::string_view, 8> googleOAuthScopes{
        "https://www.googleapis.com/auth/classroom.courses.readonly",
        "https://www.googleapis.com/auth/classroom.rosters.readonly",
        "https://www.googleapis.com/auth/classroom.profile.emails",
        "https://www.googleapis.com/auth/classroom.coursework.students.readonly",
        "https://www.googleapis.com/auth/drive.readonly",
        "openid",
        "email",
        "profile"
    };
}

constexpr std::string_view GOOGLE_CLASSROOM_HOST = "classroom.googleapis.com";
constexpr std::string_view GOOGLE_HOST = "www.googleapis.com";

namespace X = boost::asio::experimental;

namespace Network {
    Server::Server(
        asio::io_context &io,
        const std::string &address,
        const std::string &port)
        : ioc_(io), address_(address), port_(port),
          databaseSession(std::make_shared<Data::Database>(
              "host=localhost port=6432 dbname=db user=user password=pass"
          )) {
    }

    void Server::start() { asio::co_spawn(ioc_, listen(), asio::detached); }

    const std::unordered_map<std::string, Server::RequesType> Server::changeReqToEnum = {
        {"/api/analyze", GetStudentAnalyzes},
        {"/api/analyze_fragments", GetFragmentAnalyzes},
        {"/api/auth/google/start", AuthGoogleStart},
        {"/api/auth/google/callback", AuthGoogleCallback},
        {"/api/auth/me", AuthMe},
        {"/api/auth/logout", AuthLogout}
    };

    asio::awaitable<void> Server::doSession(tcp_stream stream) {
        beast::flat_buffer buffer;

        try {
            beast::get_lowest_layer(stream).expires_after(std::chrono::seconds(10));

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
        } catch (const boost::system::system_error &e) {
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
                tcp_stream stream(std::move(socket));
                asio::co_spawn(ioc_, doSession(std::move(stream)), asio::detached);
            }
        } catch (const std::exception &e) {
            std::println(std::cerr, "Exception in listen: {}", e.what());
        }
    }

    void Server::applyCorsHeaders(http::response<http::string_body> &res) const {
        const auto appOrigin = config["APP_ORIGIN"];
        if (!appOrigin.empty()) {
            res.set(http::field::access_control_allow_origin, appOrigin);
        }
        res.set("Access-Control-Allow-Credentials", "true");
        res.set(http::field::access_control_allow_methods, "GET, POST, OPTIONS");
        res.set(http::field::access_control_allow_headers, "Content-Type, Authorization");
    }

    asio::awaitable<http::response<http::string_body> > Server::requestHandler(http::request<http::string_body> req) {
        if (req.method() == http::verb::options) {
            http::response<http::string_body> res{http::status::no_content, req.version()};
            co_return res;
        }

        auto parsedTarget = boost::urls::parse_origin_form(req.target());

        if (!parsedTarget) {
            co_return http::response<http::string_body>{http::status::bad_request, req.version()};
        }

        std::string target = std::string(parsedTarget.value().path());

        if (target == "/api/classroom/courses" || target.starts_with("/api/classroom/courses/")) {
            co_return co_await classroomProxyHandler(std::move(req), parsedTarget.value());
        }

        auto it = changeReqToEnum.find(target);
        if (it == changeReqToEnum.end()) {
            co_return http::response<http::string_body>{http::status::not_found, req.version()};
        }

        switch (auto &&[_, value] = *it; value) {
            case GetStudentAnalyzes:
                if (req.method() == http::verb::post) {
                    co_return co_await analyzesHandler(std::move(req));
                }
                co_return http::response<http::string_body>{http::status::bad_request, req.version()};
            case AuthGoogleStart:
                if (req.method() == http::verb::get) {
                    co_return co_await authGoogleStartHandler(std::move(req));
                }
                co_return http::response<http::string_body>{http::status::method_not_allowed, req.version()};
            case AuthGoogleCallback:
                if (req.method() == http::verb::get) {
                    co_return co_await authGoogleCallbackHandler(std::move(req));
                }
                co_return http::response<http::string_body>{http::status::method_not_allowed, req.version()};
            case AuthLogout:
                if (req.method() == http::verb::post) {
                    co_return co_await authLogoutHandler(std::move(req));
                }
                co_return http::response<http::string_body>{http::status::method_not_allowed, req.version()};
            case AuthMe:
                if (req.method() == http::verb::get) {
                    co_return co_await authMeHandler(std::move(req));
                }
                co_return http::response<http::string_body>{http::status::method_not_allowed, req.version()};
            case GetFragmentAnalyzes:
                if (req.method() == http::verb::post) {
                    co_return co_await analyzesFragmentsHandler(std::move(req));
                }
                co_return http::response<http::string_body>{http::status::method_not_allowed, req.version()};
        }
    }

    asio::awaitable<http::response<http::string_body> > Server::analyzesHandler(http::request<http::string_body> req) {
        std::vector<Document> doc_vec;
        std::string userId;

        auto parsedTarget = boost::urls::parse_origin_form(req.target());
        if (!parsedTarget) {
            co_return http::response<http::string_body>{http::status::bad_request, req.version()};
        }

        auto tgId = parsedTarget->params().find("telegram_id");
        if (tgId != parsedTarget->params().end()) {
            try {
                int64_t telegramId = std::stoll((*tgId).value);
                auto userOpt = co_await databaseSession->selectAuthUserByTelegramId(telegramId);
                if (!userOpt.has_value()) {
                    co_return http::response<http::string_body>{http::status::unauthorized, req.version()};
                }
                userId = userOpt->id;
            } catch (...) {
                co_return http::response<http::string_body>{http::status::bad_request, req.version()};
            }
        } else {
            auto [session, _] = co_await getSessionFromCookie(req);
            if (session == std::nullopt) {
                co_return http::response<http::string_body>{http::status::unauthorized, req.version()};
            }
            userId = session->userId;
        }

        Auth::GoogleTokenManager tokenManager{
            ioc_.get_executor(),
            databaseSession,
            config
        };

        auto accessToken = co_await tokenManager.getValidAccessToken(userId);
        if (accessToken == std::nullopt) {
            co_return http::response<http::string_body>{http::status::unauthorized, req.version()};
        }

        auto json_data = boost::json::parse(req.body());

        std::vector<DocumentRequest> req_vec;

        for (auto &&item: json_data.at("filesList").as_array()) {
            const auto &file_obj = item.at("file");
            auto file_id = std::string(file_obj.at("file_id").as_string());

            auto &&result = co_await databaseSession->selectDocument(file_id);
            if (result.has_value()) {
                doc_vec.push_back(result.value());
                continue;
            }

            auto file_url = std::string(file_obj.at("file_url").as_string());
            auto file_type = std::string(file_obj.at("file_type").as_string());
            auto file_name = std::string(file_obj.at("file_name").as_string());
            http::request<http::string_body> g_req{http::verb::get, "/drive/v3/files/" + file_id + "?alt=media", 11};
            g_req.set(http::field::authorization, "Bearer " + accessToken.value());
            g_req.set(http::field::host, GOOGLE_HOST);
            g_req.keep_alive(req.keep_alive());

            req_vec.push_back({.req = g_req, .id = file_id, .file_type = file_type, .file_name = file_name});
        }

        auto res = co_await handle_document_request(req_vec, doc_vec, tp.get_executor());
        co_return res;
    }

    asio::awaitable<http::response<http::string_body>> Server::analyzesFragmentsHandler(
        http::request<http::string_body> req) {
        auto json = boost::json::parse(std::move(req.body())).as_object();

        std::string fragment_name= std::string(json.at("fragment_name").as_string());
        std::string firstDocId = std::string(json.at("first_doc_id").as_string());
        std::string secondDocId = std::string(json.at("second_doc_id").as_string());

        auto databaseResponse = co_await databaseSession->selectTwoDocumentFragments(firstDocId, secondDocId, fragment_name);

        if (databaseResponse == std::nullopt) {
            co_return http::response<http::string_body> {http::status::service_unavailable, req.version()};
        }

        auto [first, second] = databaseResponse.value();

        boost::json::value jvResult = {
            {"first_document_id", first.document_id},
            {"second_document_id", second.document_id},
            {"first_fragment_id", first.fragment_id},
            {"second_fragment_id", second.fragment_id},
        };

        auto body = boost::json::serialize(jvResult);

        http::request<http::string_body> request{http::verb::get, "/fragments_analysis", 11};
        request.set(http::field::content_type, "application/json");
        request.set(http::field::host, config["ML_SERVER_HOST"]);
        request.body() = body;
        request.prepare_payload();

        auto session = std::make_shared<SimpleSession>(ioc_.get_executor());
        auto res_message = co_await session->sendRequest<http::string_body>(request);

        http::response<http::string_body> res{http::status::ok, 11};
        res.body() = res_message.body();
        co_return res;
    }

    asio::awaitable<void> Server::saveDocumentsHandler(std::shared_ptr<std::vector<Document> > container) {
        for (auto &&doc: *container) {
            co_await databaseSession->insertDocument(doc);
        }
    }

    asio::awaitable<http::response<http::string_body> >
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

        boost::urls::url_view reqUrl{req.target()};
        auto params = reqUrl.params();
        auto it = params.find("telegram_id");
        if (it == params.end()) {
            auto res = http::response<http::string_body>{http::status::bad_request, req.version()};
            res.body() = "In query params not found 'telegram_id'";
            res.prepare_payload();
            co_return res;
        }
        int64_t telegramId = 0;
        try {
            telegramId = std::stoll((*it).value);
        } catch (...) {
            http::response<http::string_body> res{http::status::bad_request, req.version()};
            res.body() = "Invalid telegram_id";
            res.prepare_payload();
            co_return res;
        }

        if (auto stateResult = co_await databaseSession->insertOAuthState(randomTokenHash, telegramId,expiresAt); !stateResult) {
            http::response<http::string_body> res{http::status::internal_server_error, req.version()};
            res.body() = "Failed to create OAuth state";
            res.prepare_payload();
            co_return res;
        }

        auto scope = googleOAuthScopes | std::views::join_with(" "sv) | std::ranges::to<std::string>();

        std::string_view baseUrl = "https://accounts.google.com/o/oauth2/v2/auth";
        boost::urls::url redirectUrl{baseUrl};

        auto params_r = redirectUrl.params();
        params_r.append({"client_id", config["GOOGLE_CLIENT_ID"]});
        params_r.append({"redirect_uri", config["GOOGLE_REDIRECT_URI"]});
        params_r.append({"response_type", "code"});
        params_r.append({"scope", scope});
        params_r.append({"access_type", "offline"});
        params_r.append({"include_granted_scopes", "true"});
        params_r.append({"state", randomToken});

        http::response<http::string_body> res{http::status::found, req.version()};
        res.set(http::field::location, redirectUrl.buffer());
        res.prepare_payload();

        co_return res;
    }

    asio::awaitable<http::response<http::string_body> >
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

        auto telegramIdOpt = co_await databaseSession->consumeOAuthState(stateHash, now);

        if (!telegramIdOpt.has_value()) {
            co_return http::response<http::string_body>{http::status::unauthorized, req.version()};
        }

        int64_t telegramId = telegramIdOpt.value();

        Type::GoogleUserInfo client;
        Type::GoogleTokenResponse token;
        try {
            GoogleOAuthClient googleOAuthClient{ioc_.get_executor()};
            token = co_await googleOAuthClient.exchangeCodeForTokens((*code).value);
            client = co_await googleOAuthClient.fetchUserInfo(token.accessToken);
        } catch (const std::exception &e) {
            std::println(std::cerr, "Google OAuth callback error: {}", e.what());
            http::response<http::string_body> res{http::status::bad_gateway, req.version()};
            res.body() = "Google OAuth request failed";
            res.prepare_payload();
            co_return res;
        }

        auto user = co_await databaseSession->selectAuthUserByGoogleSub(client.sub);
        std::optional<Type::AuthUser> authUser;
        auto loginAt = util::time::getCurrentTimestamp();

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
        } else if (user.has_value()) {
            auto existingTokens = co_await databaseSession->selectGoogleOAuthTokens(user->id);
            refresh_token_enc = existingTokens.and_then([](const Type::GoogleOAuthTokens &tokens) {
                return tokens.refreshTokenEnc;
            });
        }

        auto expiresAt = util::time::getCurrentTimeAfterSeconds(token.expiresIn);
        Type::GoogleOAuthTokens oauthTokens{
            .userId = "",
            .accessTokenEnc = access_token_enc,
            .refreshTokenEnc = refresh_token_enc,
            .expiresAt = expiresAt,
            .scope = token.scope,
            .tokenType = token.tokenType
        };

        if (!user.has_value()) {
            authUser = co_await databaseSession->registerNewAuthUserWithTokens(
                client.sub, client.email, client.name, client.pictureUrl, loginAt, oauthTokens
            );
        } else {
            oauthTokens.userId = user->id;
            auto updateSuccess = co_await databaseSession->updateAuthUserLoginWithTokens(
                user->id, client.email, client.name, client.pictureUrl, loginAt, oauthTokens
            );
            if (!updateSuccess) {
                co_return http::response<http::string_body>{http::status::internal_server_error, req.version()};
            }
            authUser = user;
        }

        if (!authUser.has_value()) {
            co_return http::response<http::string_body>{http::status::internal_server_error, req.version()};;
        }

        bool linkSuccess = co_await databaseSession->linkTelegramIdToUser(authUser->id, telegramId);
        if (!linkSuccess) {
            std::println(std::cerr, "Failed to link telegram_id {} to user {}", telegramId, authUser->id);
            co_return http::response<http::string_body>{http::status::internal_server_error, req.version()};
        }

        std::string botUsername = std::string(config["TELEGRAM_BOT_USERNAME"]);
        if (botUsername.empty()) botUsername = "KiraACR_bot";

        std::string htmlBody = std::format(R"html(
<!DOCTYPE html>
<html lang="ru">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Авторизация успешна</title>
    <style>
        body {{ font-family: sans-serif; display: flex; align-items: center; justify-content: center; height: 100vh; background-color: #f4f6f9; margin: 0; }}
        .card {{ background: white; padding: 40px; border-radius: 16px; box-shadow: 0 4px 20px rgba(0,0,0,0.08); text-align: center; }}
        h1 {{ color: #2c3e50; font-size: 24px; }}
        p {{ color: #7f8c8d; margin-bottom: 20px; }}
        .btn {{ background-color: #2481cc; color: white; text-decoration: none; padding: 12px 24px; border-radius: 8px; font-weight: bold; display: inline-block; }}
    </style>
</head>
<body>
    <div class="card">
        <h1>✅ Подключено!</h1>
        <p>Google Classroom (<b>{}</b>) успешно привязан.</p>
        <a href="tg://resolve?domain={}" class="btn">Вернуться в Telegram</a>
    </div>
    <script>
        setTimeout(() => {{ window.location.href = "tg://resolve?domain={}"; }}, 1500);
    </script>
</body>
</html>
        )html", client.email, botUsername, botUsername);

        http::response<http::string_body> res{http::status::ok, req.version()};
        res.set(http::field::content_type, "text/html; charset=utf-8");
        res.body() = htmlBody;
        res.prepare_payload();

        co_return res;
    }

    asio::awaitable<http::response<http::string_body> > Server::authMeHandler(http::request<http::string_body> req) {
        auto [session, sessionHash] = co_await getSessionFromCookie(req);

        if (session == std::nullopt) {
            co_return http::response<http::string_body>{http::status::unauthorized, req.version()};
        }

        auto now = util::time::getCurrentTimestamp();

        auto sessionUpdate = co_await databaseSession->updateAppSessionLastSeen(sessionHash, now);

        if (!sessionUpdate) {
            std::println(std::cerr, "Failed to update app session last_seen_at");
        }

        auto user = co_await databaseSession->selectAuthUserById(session->userId);
        if (user == std::nullopt) {
            co_return http::response<http::string_body>{http::status::unauthorized, req.version()};
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

    asio::awaitable<http::response<http::string_body> >
    Server::authLogoutHandler(http::request<http::string_body> req) {
        std::string_view cookieName = config["SESSION_COOKIE_NAME"];
        if (cookieName.empty()) {
            cookieName = "anty_session";
        }

        auto sessionId = getCookie(req, cookieName);
        if (sessionId == std::nullopt) {
            co_return http::response<http::string_body>{http::status::no_content, req.version()};
        }

        auto sessionHash = util::sha256Hex(sessionId.value());
        auto now = util::time::getCurrentTimestamp();

        co_await databaseSession->revokeAppSession(sessionHash, now);

        http::response<http::string_body> res{http::status::no_content, req.version()};
        auto cookie_value = std::format(
            "{}=; HttpOnly; SameSite=None; Secure Path=/; Max-Age=0",
            cookieName
        );

        res.set(http::field::set_cookie, cookie_value);
        res.prepare_payload();
        co_return res;
    }

    asio::awaitable<http::response<http::string_body> > Server::classroomProxyHandler(
        http::request<http::string_body> req, boost::url_view target) {
        if (req.method() != http::verb::get) {
            co_return http::response<http::string_body>{http::status::method_not_allowed, req.version()};
        }

        if (!util::network::verifPath(target)) {
            co_return http::response<http::string_body>{http::status::bad_request, req.version()};
        }

        boost::urls::url mutable_target(target);
        std::string userId;

        auto tgId = mutable_target.params().find("telegram_id");
        if (tgId != mutable_target.params().end()) {
            int64_t telegramId = std::stoll((*tgId).value);

            auto userOpt = co_await databaseSession->selectAuthUserByTelegramId(telegramId);
            if (!userOpt.has_value()) {
                co_return http::response<http::string_body>{http::status::unauthorized, req.version()};
            }
            userId = userOpt->id;

            mutable_target.params().erase(tgId);
        } else {
            auto [session , _] = co_await getSessionFromCookie(req);
            if (session == std::nullopt) {
                co_return http::response<http::string_body>{http::status::unauthorized, req.version()};
            }
            userId = session->userId;
        }

        constexpr std::string_view prefix = "/api/classroom";
        auto path = mutable_target.encoded_path();

        std::string newTarget = "/v1";
        newTarget += path.substr(prefix.size());

        if (!mutable_target.encoded_query().empty()) {
            newTarget += "?";
            newTarget += mutable_target.encoded_query();
        } else if (path == "/api/classroom/courses") {
            newTarget += "?courseStates=ACTIVE";
        }

        http::request<http::string_body> request{http::verb::get, newTarget, 11};
        Auth::GoogleTokenManager tokenManager{
            ioc_.get_executor(),
            databaseSession,
            config
        };

        auto token = co_await tokenManager.getValidAccessToken(userId);
        if (token == std::nullopt) {
            co_return http::response<http::string_body>{http::status::unauthorized, req.version()};
        }

        request.set(http::field::content_type, "application/json");
        request.set(http::field::authorization, "Bearer " + token.value());
        request.set(http::field::host, GOOGLE_CLASSROOM_HOST);
        request.prepare_payload();

        auto googleSession = std::make_shared<SslSession>(ioc_.get_executor());
        auto googleResponse = co_await googleSession->sendRequest<http::string_body>(std::move(request));

        co_return googleResponse;
    }

    asio::awaitable<http::response<http::string_body> > Server::handle_document_request(
        std::vector<DocumentRequest> vreq, std::span<Document> cache_docs, asio::any_io_executor cpu_ex) {
        auto container = std::make_shared<std::vector<Document> >();

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

            for (const auto &i: errors) {
                if (i) {
                    std::rethrow_exception(i);
                }
            }

            asio::co_spawn(tp.get_executor(), saveDocumentsHandler(container), asio::detached);
        }

        http::request<http::string_body> request{http::verb::post, "/analysis", 11};

        boost::json::array obj_array;

        if (!cache_docs.empty()) {
            std::ranges::copy(cache_docs, std::back_inserter(*container));
        }

        for (auto &&item: *container) {
            boost::json::value jv = boost::json::value_from(item);
            obj_array.emplace_back(jv);
        }

        boost::json::object options;
        options["strict_titles"] = false;
        options["document_limit"] = 7;
        options["jobs"] = 8;

        boost::json::object payload;
        payload["documents"] = std::move(obj_array);
        payload["options"] = std::move(options);

        request.body() = boost::json::serialize(payload);
        request.set(http::field::content_type, "application/json");
        request.set(http::field::host, config["ML_SERVER_HOST"]);
        request.prepare_payload();

        auto session = std::make_shared<SimpleSession>(ioc_.get_executor());
        auto res_message = co_await session->sendRequest<http::string_body>(request);

        http::response<http::string_body> res{http::status::ok, 11};
        res.body() = res_message.body();
        co_return res;
    }

    asio::awaitable<void> Server::download_extract_store(
        DocumentRequest req, asio::any_io_executor cpu_ex, asio::strand<asio::any_io_executor> store_strand,
        std::shared_ptr<std::vector<Document> > container) const {
        auto download_session = std::make_shared<SslSession>(ioc_.get_executor());
        auto doc_req = co_await download_session->downloadWithRedirect(req.req);

        std::println(std::cout, "Попытка скачать файл {}.", req.id);

        if (doc_req.result() != http::status::ok) {
            auto temp = std::string(doc_req.body().begin(), doc_req.body().end());

            std::println(
                std::cerr, "ОШИБКА СКАЧИВАНИЯ {}: Код {}. Тело: {}", req.id, static_cast<unsigned>(doc_req.result()),
                std::string(doc_req.body().begin(), doc_req.body().end()));
        }

        co_await asio::post(cpu_ex, asio::use_awaitable);

        auto doc_text = DocReader::DocumentReaderFromRaw(doc_req.body(), req.file_type);

        co_await asio::post(store_strand, asio::use_awaitable);

        container->emplace_back(std::move(doc_text.value()), req.id, req.file_name);
    }

    template<typename T>
    std::optional<std::string> Server::getCookie(const http::request<T> &req, std::string_view cookieName) {
        auto cookie = util::network::parse_cookie(req[http::field::cookie]);
        for (auto &&[key, value]: cookie) {
            if (key == cookieName) {
                return value;
            }
        }

        return std::nullopt;
    }

    asio::awaitable<std::tuple<std::optional<Type::AppSession>, std::string> > Server::getSessionFromCookie(
        http::request<http::string_body> &req) {
        std::string_view cookieName = config["SESSION_COOKIE_NAME"];
        if (cookieName.empty()) {
            cookieName = "anty_session";
        }

        auto sessionId = getCookie(req, cookieName);
        if (sessionId == std::nullopt) {
            co_return std::make_tuple(std::nullopt, "");
        }

        const auto sessionHash = util::sha256Hex(sessionId.value());
        std::string now;
        now = util::time::getCurrentTimestamp();

        auto session = co_await databaseSession->selectActiveAppSession(sessionHash, now);
        co_return std::make_tuple(session, std::move(sessionHash));
    }
} // namespace Network
