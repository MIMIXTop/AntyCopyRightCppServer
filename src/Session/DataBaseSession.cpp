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
    req.set(http::field::content_type, "application/json");
    req.set(http::field::authorization, config["SUPABASE_KEY"]);
    req.set("apikey", config["SUPABASE_KEY"]);
    req.set(http::field::host, config["SUPABASE_HOST"]);
    req.set("Prefer", "return=representation");
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
    sectionsReq.set(http::field::content_type, "application/json");
    sectionsReq.set("apikey", config["SUPABASE_KEY"]);
    sectionsReq.set(http::field::authorization, config["SUPABASE_KEY"]);
    sectionsReq.set(http::field::host, config["SUPABASE_HOST"]);
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
    requestToGetListDocumentSections.set(http::field::content_type, "application/json");
    requestToGetListDocumentSections.set(http::field::authorization, config["SUPABASE_KEY"]);
    requestToGetListDocumentSections.set(http::field::host, config["SUPABASE_HOST"]);
    requestToGetListDocumentSections.set("apikey", config["SUPABASE_KEY"]);
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
    req.set(http::field::content_type, "application/json");
    req.set(http::field::authorization, config["SUPABASE_KEY"]);
    req.set(http::field::host, config["SUPABASE_HOST"]);
    req.set("apikey", config["SUPABASE_KEY"]);
    req.prepare_payload();

    auto res = co_await databaseSession->sendRequest<http::string_body>(std::move(req));
    if (auto status = res.result(); status != http::status::ok && status != http::status::no_content) {
        co_return false;
    }
    co_return true;
}

}   // namespace Network
