#include "Database.hpp"
#include "Util/AsyncExecution.hpp"

#include <iostream>
#include <thread>

namespace Network::Data {
    Database::Database(std::string_view connectionString)
        : connectionString_(connectionString), pool_(std::max(1u, std::thread::hardware_concurrency() / 2)) {
    }

    std::shared_ptr<Database> Database::get_ptr() {
        return shared_from_this();
    }

    boost::asio::awaitable<bool> Database::insertDocument(Document document) {
        bool success = co_await Util::Async::AsyncExecute(pool_, [this, doc = std::move(document)] mutable -> bool {
            try {
                auto connection = getConnection();
                pqxx::work txn(*connection);

                std::string insertDocHeaderSql = R"(
                    INSERT INTO documents (external_id, title)
                    VALUES ($1, $2)
                    RETURNING id;
                )";

                auto res = txn.exec_params1(insertDocHeaderSql, doc.docId, doc.title);

                auto docId = res[0].as<std::string>();
                std::string insertSectionSql = R"(
                    INSERT INTO document_sections (document_id, title, content)
                    VALUES ($1, $2, $3);
                )";

                std::ranges::for_each(doc.text, [&](const auto &text) {
                    txn.exec_params(insertSectionSql, docId, text.title, text.text);
                });
                txn.commit();
                return true;
            } catch (const pqxx::sql_error &e) {
                std::cerr << "SQL error: " << e.what() << " Query: " << e.query() << '\n';
                return false;
            } catch (const std::exception &e) {
                std::cerr << "General error: " << e.what() << '\n';
                return false;
            }
        });

        co_return success;
    }

    boost::asio::awaitable<bool> Database::deleteDocument(std::string_view documentId) {
        bool success = co_await Util::Async::AsyncExecute(pool_, [this, documentId] mutable -> bool {
            try {
                auto connection = getConnection();
                pqxx::work txn(*connection);
                std::string deleteDocumentSql = R"(
                    DELETE FROM documents WHERE external_id = $1
                )";
                txn.exec_params(deleteDocumentSql, documentId);

                txn.commit();
                return true;
            } catch (const pqxx::sql_error &e) {
                std::cerr << "SQL error: " << e.what() << " Query: " << e.query() << '\n';
                return false;
            } catch (const std::exception &e) {
                std::cerr << "General error: " << e.what() << '\n';
                return false;
            }
        });

        co_return success;
    }

    boost::asio::awaitable<std::optional<Document> > Database::selectDocument(std::string documentId) {
        auto document = co_await Util::Async::AsyncExecute(
            pool_, [this, documentId] mutable -> std::optional<Document> {
                try {
                    auto connection = getConnection();
                    pqxx::work txn(*connection);
                    std::string selectDocumentSql = R"(
                        SELECT * FROM documents WHERE external_id = $1
                    )";

                    pqxx::result res = txn.exec_params(selectDocumentSql, documentId);
                    if (res.empty()) {
                        return std::nullopt;
                    }

                    auto row = res[0];

                    std::string internalDbId = row["id"].as<std::string>();
                    std::string extId = row["external_id"].as<std::string>();

                    std::string title = "";
                    if (!row["title"].is_null()) {
                        title = row["title"].as<std::string>();
                    }

                    std::string selectSectionsSql = R"(
                        SELECT title, content
                        FROM document_sections
                        WHERE document_id = $1;
                    )";

                    pqxx::result secRes = txn.exec_params(selectSectionsSql, internalDbId);

                    std::vector<Documents::Paragraph> paragraphs;
                    paragraphs.reserve(secRes.size());

                    for (const auto &secRow: secRes) {
                        paragraphs.emplace_back(
                            secRow["title"].as<std::string>(),
                            secRow["content"].as<std::string>()
                        );
                    }
                    return Document(std::move(paragraphs), std::move(extId), std::move(title));
                } catch (const pqxx::sql_error &e) {
                    std::cerr << "SQL error: " << e.what() << " Query: " << e.query() << '\n';
                } catch (const std::exception &e) {
                    std::cerr << "General error: " << e.what() << '\n';
                }
                return std::nullopt;
            });
        co_return document;
    }

    std::unique_ptr<pqxx::connection> Database::getConnection() {
        return std::make_unique<pqxx::connection>(connectionString_);
    }
}
