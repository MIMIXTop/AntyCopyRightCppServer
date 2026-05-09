//
// Created by mimixtop on 06.05.2026.
//

#include "DataBaseSession.hpp"

#include <boost/beast.hpp>
#include <boost/beast/http/message.hpp>

#include <format>

namespace {
    namespace asio = boost::asio;
    namespace beast = boost::beast;
    namespace http = beast::http;

    void setSupabaseHeaders(http::request<http::string_body>& request, const Util::ConfigParser& config) {
        request.set(http::field::authorization, config["SUPABASE_KEY"]);
        request.set("apikey", config["SUPABASE_KEY"]);
        request.set(http::field::host, config["SUPABASE_HOST"]);
        request.set(http::field::content_type, "application/json");
    }

}

namespace Network {

DataBaseSession::DataBaseSession() : databaseSession(std::make_shared<SslSession>(threadPool.get_executor())) {}

asio::awaitable<bool> DataBaseSession::insertDocument(const Document& document) {
    boost::json::object documentJson {
        {"external_id", document.docId},
        {"title", document.text.empty() ? document.docId : document.text.front().title},
    };

    http::request<http::string_body> req{http::verb::post,  baseUrl + "/documents?select=id", 11};

    req.body() = boost::json::serialize(documentJson);
    req.set("Prefer", "return=representation");

    setSupabaseHeaders(req, config);
    req.prepare_payload();

    auto res = co_await databaseSession->sendRequest<http::string_body>(std::move(req));
    if (auto status = res.result(); status != http::status::created && status != http::status::ok) {
        co_return false;
    }

    auto insertedDocuments = boost::json::parse(res.body()).as_array();
    if (insertedDocuments.empty()) {
        co_return false;
    }

    auto const& documentId = insertedDocuments.front().as_object().at("id").as_string();

    if (document.text.empty()) {
        co_return true;
    }

    boost::json::array sectionsJson;
    sectionsJson.reserve(document.text.size());

    for (auto const& paragraph : document.text) {
        sectionsJson.emplace_back(boost::json::object {
            {"document_id", documentId},
            {"title", paragraph.title},
            {"content", paragraph.text},
        });
    }

    http::request<http::string_body> sectionsReq{http::verb::post,  baseUrl + "/document_sections", 11};

    sectionsReq.body() = boost::json::serialize(sectionsJson);
    sectionsReq.set("Prefer", "return=representation");
    setSupabaseHeaders(sectionsReq, config);
    sectionsReq.prepare_payload();

    auto sectionsRes = co_await databaseSession->sendRequest<http::string_body>(std::move(sectionsReq));
    if (auto status = sectionsRes.result(); status != http::status::created && status != http::status::ok) {
        co_return false;
    }

    co_return true;
}

asio::awaitable<std::optional<Document>> DataBaseSession::selectDocumentById(std::string_view documentId) {
    std::string target = std::format(
        "/rest/v1/documents?external_id=eq.{}&select=id,external_id,title,document_sections(id,title,content)",
        documentId
    );

    http::request<http::string_body> requestToGetListDocumentSections{http::verb::get, target, 11};
    requestToGetListDocumentSections.set("Prefer", "return=representation");
    setSupabaseHeaders(requestToGetListDocumentSections, config);
    requestToGetListDocumentSections.prepare_payload();

    auto resToDocumentSections = co_await databaseSession->sendRequest<http::string_body>(std::move(requestToGetListDocumentSections));

    if (const auto status = resToDocumentSections.result(); status != http::status::ok) {
        co_return std::nullopt;
    }

    auto json = boost::json::parse(resToDocumentSections.body());
    auto const& documents = json.as_array();

    if (documents.empty()) {
        co_return std::nullopt;
    }

    co_return boost::json::value_to<Document>(documents.front());

}
asio::awaitable<bool> DataBaseSession::deleteDocument(std::string_view documentId) {
    std::string target = std::format(
        "/rest/v1/documents?external_id=eq.{}",
        documentId
    );

    http::request<http::string_body> req {http::verb::delete_, target, 11};
    req.set("Prefer", "return=representation");
    setSupabaseHeaders(req, config);
    req.prepare_payload();

    auto res = co_await databaseSession->sendRequest<http::string_body>(std::move(req));
    if (auto status = res.result(); status != http::status::ok && status != http::status::no_content) {
        co_return false;
    }
    co_return true;
}
asio::awaitable<bool> DataBaseSession::insertOAuthState(std::string_view stateHash, std::string_view expiresAt) {
    std::string target = std::format(
        "/rest/v1/oauth_states"
    );

    http::request<http::string_body> req{http::verb::post, target, 11};
    setSupabaseHeaders(req, config);
    boost::json::object json;

    json["state_hash"] = stateHash;
    json["expires_at"] = expiresAt;
    req.body() = boost::json::serialize(json);
    req.prepare_payload();

    auto res = co_await databaseSession->sendRequest<http::string_body>(std::move(req));
    if (auto status = res.result(); status != http::status::created && status != http::status::ok) {
        co_return false;
    }
    co_return true;
}

asio::awaitable<bool>
DataBaseSession::consumeOAuthState(std::string_view stateHash, std::string_view consumedAt) {
    std::string target = std::format(
    "/rest/v1/oauth_states?state_hash=eq.{}&consumed_at=is.null&expires_at=gt.{}" ,
    stateHash,
    consumedAt
    );

    http::request<http::string_body> req{http::verb::patch, target, 11};
    req.set("Prefer", "return=representation");
    setSupabaseHeaders(req, config);

    boost::json::object json;
    json["consumed_at"] = consumedAt;
    req.body() = boost::json::serialize(json);
    req.prepare_payload();

    auto res = co_await databaseSession->sendRequest<http::string_body>(std::move(req));
    if (auto status = res.result(); status != http::status::created && status != http::status::ok) {
        co_return false;
    }
    auto body = boost::json::parse(res.body());

    auto arr = body.as_array();
    if (arr.empty()) {
        co_return false;
    }
    co_return true;

}

}   // namespace Network
