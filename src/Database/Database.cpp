#include "Database.hpp"
#include "Util/AsyncExecution.hpp"

#include <iostream>
#include <thread>

namespace Network::Data {
    Database::Database(std::string_view connectionString)
        : connectionString_(connectionString), pool_(std::max(4u, std::thread::hardware_concurrency() / 2)) {
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

                auto res = txn.exec(insertDocHeaderSql, pqxx::params{doc.docId, doc.title}).one_row();

                auto docId = res[0].as<std::string>();
                std::string insertSectionSql = R"(
                    INSERT INTO document_sections (document_id, title, content)
                    VALUES ($1, $2, $3);
                )";

                std::ranges::for_each(doc.text, [&](const auto &text) {
                    txn.exec(insertSectionSql, pqxx::params{docId, text.title, text.text});
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
                txn.exec(deleteDocumentSql, pqxx::params{documentId});

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

                    pqxx::result res = txn.exec(selectDocumentSql, pqxx::params{documentId});
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

                    pqxx::result secRes = txn.exec(selectSectionsSql, pqxx::params{internalDbId});

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

    boost::asio::awaitable<bool> Database::insertOAuthState(std::string_view stateHash, std::string_view expiresAt) {
        bool success = co_await Util::Async::AsyncExecute(pool_, [this, stateHash, expiresAt] mutable -> bool {
            try {
                auto connection = getConnection();
                pqxx::work txn(*connection);
                std::string insertStateSql = R"(
                    INSERT INTO oauth_states (state_hash, expires_at) VALUES ($1, $2)
                )";
                txn.exec(insertStateSql, pqxx::params{stateHash, expiresAt});
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

    boost::asio::awaitable<bool> Database::consumeOAuthState(std::string_view stateHash, std::string_view consumedId) {
        bool success = co_await Util::Async::AsyncExecute(pool_, [this, stateHash, consumedId] mutable -> bool {
            try {
                auto connection = getConnection();
                pqxx::work txn(*connection);
                std::string updateStateSql = R"(
                    UPDATE oauth_states
                    SET consumed_at = $1
                    WHERE
                        state_hash = $2
                        AND consumed_at IS NULL
                        AND expires_at > $3
                )";

                txn.exec(updateStateSql, pqxx::params{consumedId, stateHash, consumedId});
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

    boost::asio::awaitable<std::optional<Type::AuthUser>> Database::selectAuthUserByGoogleSub(
        std::string_view googleSub
        ) {
        auto authUser = co_await Util::Async::AsyncExecute(pool_, [this, googleSub] mutable -> std::optional<Type::AuthUser> {
            try {
                auto connection = getConnection();
                pqxx::work txn(*connection);
                std::string selectAuthUserSql = R"(
                    SELECT
                        id,
                        google_sub,
                        email,
                        name,
                        picture_url
                    FROM auth_users
                    WHERE google_sub = $1
                )";

                pqxx::result authUserRow = txn.exec(selectAuthUserSql, pqxx::params{googleSub});
                txn.commit();
                if (authUserRow.empty()) {
                    return std::nullopt;
                }

                auto row = authUserRow[0];

                const auto email = row["email"].as<std::string>();
                const auto name = row["name"].as<std::string>();
                const auto picture_url = row["picture_url"].as<std::string>();
                const auto id = row["id"].as<std::string>();
                const auto google_sub = row["google_sub"].as<std::string>();

                return Type::AuthUser {
                    .id = id,
                    .googleSub = google_sub,
                    .email = email,
                    .name = name,
                    .pictureUrl = picture_url
                };

            } catch (const pqxx::sql_error &e) {
                std::cerr << "SQL error: " << e.what() << " Query: " << e.query() << '\n';
                return std::nullopt;
            } catch (const std::exception &e) {
                std::cerr << "General error: " << e.what() << '\n';
                return std::nullopt;
            }
        });

        co_return authUser;
    }

    boost::asio::awaitable<std::optional<Type::AuthUser>> Database::insertAuthUser(std::string_view googleSub,
        std::string_view email, std::string_view name, std::string_view pictureUrl, std::string_view lastLoginAt) {
        auto authUser = co_await Util::Async::AsyncExecute(pool_, [this, googleSub, pictureUrl, email, name, lastLoginAt] mutable -> std::optional<Type::AuthUser> {
            try {
                auto connection = getConnection();
                pqxx::work txn(*connection);
                std::string insertAuthUserSql = R"(
                       INSERT INTO auth_users (google_sub, email, name, picture_url, last_login_at)
                       VALUES ($1, $2, $3, $4, $5)
                       RETURNING id
                )";

                auto res = txn.exec(
                    insertAuthUserSql,
                    pqxx::params{
                        googleSub,
                        email,
                        name,
                        pictureUrl,
                        lastLoginAt
                    }
                ).one_row();
                auto userId = res[0].as<std::string>();

                return Type::AuthUser {
                    .id = userId,
                    .googleSub = std::string(googleSub),
                    .email = std::string(email),
                    .name = std::string(name),
                    .pictureUrl = std::string(pictureUrl)
                };
            } catch (const pqxx::sql_error &e) {
                std::cerr << "SQL error: " << e.what() << " Query: " << e.query() << '\n';
                return std::nullopt;
            } catch (const std::exception &e) {
                std::cerr << "General error: " << e.what() << '\n';
                return std::nullopt;
            }
        });
        co_return authUser;
    }

    boost::asio::awaitable<std::optional<Type::AuthUser>> Database::updateAuthUserLogin(std::string_view id,
        std::string_view email, std::string_view name, std::string_view pictureUrl, std::string_view lastLoginAt) {
        auto authUser = co_await Util::Async::AsyncExecute(pool_, [this, id, email, name, lastLoginAt, pictureUrl] mutable -> std::optional<Type::AuthUser> {
            try {
                auto connection = getConnection();
                pqxx::work txn(*connection);
                std::string updateAuthUserSql = R"(
                    UPDATE auth_users
                    SET
                        email = $1,
                        name = $2,
                        picture_url = $3,
                        last_login_at = $4,
                        updated_at = $5
                    WHERE
                        id = $6
                    RETURNING google_sub
                )";

                auto row = txn.exec(updateAuthUserSql, pqxx::params{email, name, pictureUrl, lastLoginAt, lastLoginAt, id}).one_row();
                auto googleSub = row["google_sub"].as<std::string>();
                txn.commit();
                return Type::AuthUser {
                    .id = std::string(id),
                    .googleSub = googleSub,
                    .email = std::string(email),
                    .name = std::string(name),
                    .pictureUrl = std::string(pictureUrl)
                };
            } catch (const pqxx::sql_error &e) {
                std::cerr << "SQL error: " << e.what() << " Query: " << e.query() << '\n';
                return std::nullopt;
            } catch (const std::exception &e) {
                std::cerr << "General error: " << e.what() << '\n';
                return std::nullopt;
            }
        });
        co_return authUser;
    }

    boost::asio::awaitable<bool> Database::upsertGoogleOAuthTokens(const Type::GoogleOAuthTokens &tokens) {
        bool success = co_await Util::Async::AsyncExecute(pool_, [this, tokens] mutable {
            try {
                auto connection = getConnection();
                pqxx::work txn(*connection);
                std::string upsertGoogleOAuthTokensSql = R"(
                    INSERT INTO google_oauth_tokens (
                        user_id,
                        access_token_enc,
                        refresh_token_enc,
                        expires_at,
                        scope,
                        token_type
                    )
                    VALUES (
                               $1,
                               $2,
                               $3,
                               $4,
                               $5,
                               $6
                           )
                    ON CONFLICT (id)
                    DO UPDATE SET
                        access_token_enc = excluded.access_token_enc,
                        refresh_token_enc = COALESCE(excluded.refresh_token_enc, google_oauth_tokens.refresh_token_enc),
                        expires_at = excluded.expires_at,
                        scope = excluded.scope,
                        token_type = excluded.token_type
                )";

                txn.exec(
                    upsertGoogleOAuthTokensSql,
                    pqxx::params{
                        tokens.userId,
                        tokens.accessTokenEnc,
                        tokens.refreshTokenEnc,
                        tokens.expiresAt,
                        tokens.scope,
                        tokens.tokenType
                    }
                );

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

    boost::asio::awaitable<std::optional<Type::GoogleOAuthTokens>> Database::selectGoogleOAuthTokens(
        std::string_view userId) {
        auto token = co_await Util::Async::AsyncExecute(
        pool_,
        [this, userId] mutable -> std::optional<Type::GoogleOAuthTokens> {
            try {
                auto connection = getConnection();
                pqxx::work txn(*connection);
                std::string selectGoogleOAuthTokensSql = R"(
                    SELECT
                        user_id, access_token_enc, refresh_token_enc, expires_at, scope, token_type
                    FROM google_oauth_tokens
                    WHERE
                        user_id = $1
                )";

                auto result = txn.exec(
                    selectGoogleOAuthTokensSql,
                    pqxx::params{
                        userId
                    }
                );
                txn.commit();

                if (result.empty()) {
                    return std::nullopt;
                }

                auto row = result.one_row();
                std::optional<std::string> refreshTokenEnc;
                if (!row["refresh_token_enc"].is_null()) {
                    refreshTokenEnc = row["refresh_token_enc"].as<std::string>();
                }
                return Type::GoogleOAuthTokens {
                    .userId = row["user_id"].as<std::string>(),
                    .accessTokenEnc = row["access_token_enc"].as<std::string>(),
                    .refreshTokenEnc = refreshTokenEnc,
                    .expiresAt = row["expires_at"].as<std::string>(),
                    .scope = row["scope"].as<std::string>(),
                    .tokenType = row["token_type"].as<std::string>()
                };
            } catch (const pqxx::sql_error &e) {
                std::cerr << "SQL error: " << e.what() << " Query: " << e.query() << '\n';
                return std::nullopt;
            } catch (const std::exception &e) {
                std::cerr << "General error: " << e.what() << '\n';
                return std::nullopt;
            }
        }
        );
        co_return token;
    }

    boost::asio::awaitable<bool> Database::insertAppSession(std::string_view id,
        std::string_view userId, std::string_view sessionHash, std::string_view expiresAt, std::string_view userAgent) {
        auto success = co_await Util::Async::AsyncExecute(pool_, [this, id, userId, sessionHash, expiresAt, userAgent] mutable -> bool {
            try {
                auto connection = getConnection();
                pqxx::work txn(*connection);
                std::string insertAppSessionSql = R"(
                    INSERT INTO app_sessions (
                          id,
                          user_id,
                          session_hash,
                          expires_at,
                          user_agent
                    ) VALUES ($1, $2, $3, $4, $5)
                )";

                if (!id.empty()) {
                    txn.exec(
                        insertAppSessionSql,
                        pqxx::params{
                            id,
                            userId,
                            sessionHash,
                            expiresAt,
                            userAgent,
                        }
                    );
                } else {
                    std::string insertAppSessionSqlWithoutId = R"(
                        INSERT INTO app_sessions (
                              user_id,
                              session_hash,
                              expires_at,
                              user_agent
                        ) VALUES ($1, $2, $3, $4)
                    )";
                    txn.exec(
                        insertAppSessionSqlWithoutId,
                        pqxx::params{
                            userId,
                            sessionHash,
                            expiresAt,
                            userAgent,
                        }
                    );
                }

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

    boost::asio::awaitable<std::optional<Type::AppSession>> Database::selectActiveAppSession(
        std::string_view sessionHash, std::string_view now) {
        auto appSession = co_await Util::Async::AsyncExecute(pool_, [this, sessionHash, now] mutable -> std::optional<Type::AppSession> {
            try {
                auto connection = getConnection();
                pqxx::work txn(*connection);
                std::string selectAppSessionSql = R"(
                    SELECT
                        id,
                        user_id,
                        session_hash,
                        expires_at,
                        revoked_at
                    FROM app_sessions
                    WHERE
                        session_hash = $1
                        AND revoked_at IS NULL
                        AND expires_at > $2
                )";

                auto result = txn.exec(
                    selectAppSessionSql,
                    pqxx::params{
                        sessionHash,
                        now
                    }
                );
                txn.commit();

                if (result.empty()) {
                    return std::nullopt;
                }

                auto row = result.one_row();
                std::optional<std::string> revokedAt;
                if (!row["revoked_at"].is_null()) {
                    revokedAt = row["revoked_at"].as<std::string>();
                }

                return Type::AppSession {
                    .id = row["id"].as<std::string>(),
                    .userId =   row["user_id"].as<std::string>(),
                    .sessionHash = row["session_hash"].as<std::string>(),
                    .expiresAt = row["expires_at"].as<std::string>(),
                    .revokedAt = revokedAt
                };
            } catch (const pqxx::sql_error &e) {
                std::cerr << "SQL error: " << e.what() << " Query: " << e.query() << '\n';
                return std::nullopt;
            } catch (const std::exception &e) {
                std::cerr << "General error: " << e.what() << '\n';
                return std::nullopt;
            }
        });
        co_return appSession;
    }

    boost::asio::awaitable<bool> Database::revokeAppSession(std::string_view sessionHash, std::string_view revokedAt) {
        bool success = co_await Util::Async::AsyncExecute(pool_, [this, sessionHash, revokedAt] mutable -> bool {
            try {
                auto connection = getConnection();
                pqxx::work txn(*connection);
                std::string revokeAppSessionSql = R"(
                    UPDATE app_sessions
                    SET
                        revoked_at = $1
                    WHERE
                        session_hash = $2
                )";
                txn.exec(
                    revokeAppSessionSql,
                    pqxx::params{
                    revokedAt,
                        sessionHash,
                    }
                );
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

    boost::asio::awaitable<bool> Database::updateAppSessionLastSeen(std::string_view sessionHash,
        std::string_view lastSeenAt) {
        bool success = co_await Util::Async::AsyncExecute(pool_, [this, sessionHash, lastSeenAt] mutable -> bool {
            try {
                auto connection = getConnection();
                pqxx::work txn(*connection);
                std::string updateAppSessionLastSeen = R"(
                    UPDATE app_sessions
                    SET
                        last_seen_at = $1
                    WHERE
                        session_hash = $2
                )";
                txn.exec(
                    updateAppSessionLastSeen,
                    pqxx::params{
                    lastSeenAt,
                        sessionHash,
                    }
                );
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

    boost::asio::awaitable<std::optional<Type::AuthUser>> Database::registerNewAuthUserWithTokens(
        std::string_view googleSub,
        std::string_view email,
        std::string_view name,
        std::string_view pictureUrl,
        std::string_view lastLoginAt,
        const Type::GoogleOAuthTokens& tokens) {
        
        auto authUser = co_await Util::Async::AsyncExecute(pool_, 
            [this, googleSub, email, name, pictureUrl, lastLoginAt, tokens] mutable -> std::optional<Type::AuthUser> {
            try {
                auto connection = getConnection();
                pqxx::work txn(*connection);
                
                // 1. INSERT auth_users
                std::string insertAuthUserSql = R"(
                    INSERT INTO auth_users (google_sub, email, name, picture_url, last_login_at)
                    VALUES ($1, $2, $3, $4, $5)
                    RETURNING id
                )";
                
                auto res = txn.exec(
                    insertAuthUserSql, 
                    pqxx::params{googleSub, email, name, pictureUrl, lastLoginAt}
                ).one_row();
                
                auto userId = res[0].as<std::string>();
                
                // 2. INSERT google_oauth_tokens с тем же userId
                std::string insertTokensSql = R"(
                    INSERT INTO google_oauth_tokens (
                        user_id,
                        access_token_enc,
                        refresh_token_enc,
                        expires_at,
                        scope,
                        token_type
                    )
                    VALUES ($1, $2, $3, $4, $5, $6)
                )";
                
                txn.exec(
                    insertTokensSql,
                    pqxx::params{
                        userId,
                        tokens.accessTokenEnc,
                        tokens.refreshTokenEnc,
                        tokens.expiresAt,
                        tokens.scope,
                        tokens.tokenType
                    }
                );
                
                // 3. COMMIT обе операции вместе
                txn.commit();
                
                return Type::AuthUser {
                    .id = userId,
                    .googleSub = std::string(googleSub),
                    .email = std::string(email),
                    .name = std::string(name),
                    .pictureUrl = std::string(pictureUrl)
                };
                
            } catch (const pqxx::sql_error &e) {
                std::cerr << "SQL error: " << e.what() << " Query: " << e.query() << '\n';
                return std::nullopt;
            } catch (const std::exception &e) {
                std::cerr << "General error: " << e.what() << '\n';
                return std::nullopt;
            }
        });
        
        co_return authUser;
    }

    boost::asio::awaitable<bool> Database::updateAuthUserLoginWithTokens(
        std::string_view id,
        std::string_view email,
        std::string_view name,
        std::string_view pictureUrl,
        std::string_view lastLoginAt,
        const Type::GoogleOAuthTokens& tokens) {
        
        bool success = co_await Util::Async::AsyncExecute(pool_, 
            [this, id, email, name, pictureUrl, lastLoginAt, tokens] mutable -> bool {
            try {
                auto connection = getConnection();
                pqxx::work txn(*connection);
                
                // 1. UPDATE auth_users
                std::string updateAuthUserSql = R"(
                    UPDATE auth_users
                    SET
                        email = $1,
                        name = $2,
                        picture_url = $3,
                        last_login_at = $4,
                        updated_at = $5
                    WHERE
                        id = $6
                )";
                
                txn.exec(
                    updateAuthUserSql,
                    pqxx::params{email, name, pictureUrl, lastLoginAt, lastLoginAt, id}
                );
                
                // 2. UPSERT google_oauth_tokens
                std::string upsertTokensSql = R"(
                    INSERT INTO google_oauth_tokens (
                        user_id,
                        access_token_enc,
                        refresh_token_enc,
                        expires_at,
                        scope,
                        token_type
                    )
                    VALUES ($1, $2, $3, $4, $5, $6)
                    ON CONFLICT (user_id)
                    DO UPDATE SET
                        access_token_enc = excluded.access_token_enc,
                        refresh_token_enc = COALESCE(excluded.refresh_token_enc, google_oauth_tokens.refresh_token_enc),
                        expires_at = excluded.expires_at,
                        scope = excluded.scope,
                        token_type = excluded.token_type
                )";
                
                txn.exec(
                    upsertTokensSql,
                    pqxx::params{
                        id,
                        tokens.accessTokenEnc,
                        tokens.refreshTokenEnc,
                        tokens.expiresAt,
                        tokens.scope,
                        tokens.tokenType
                    }
                );
                
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

    boost::asio::awaitable<std::optional<Type::AuthUser>> Database::selectAuthUserById(std::string_view userId) {
        auto authUser = co_await Util::Async::AsyncExecute(pool_, [this, userId] mutable -> std::optional<Type::AuthUser> {
            try {
                auto connection = getConnection();
                pqxx::work txn(*connection);
                std::string authUserId = R"(
                    SELECT
                        id,
                        google_sub,
                        email,
                        name,
                        picture_url
                    FROM auth_users
                    WHERE id = $1
                )";

                auto result = txn.exec(
                    authUserId,
                    pqxx::params{
                        userId
                    }
                );
                txn.commit();

                if (result.empty()) {
                    return std::nullopt;
                }

                auto row = result.one_row();
                return Type::AuthUser {
                    .id = row["id"].as<std::string>(),
                    .googleSub = row["google_sub"].as<std::string>(),
                    .email = row["email"].as<std::string>(),
                    .name =row["name"].as<std::string>() ,
                    .pictureUrl = row["picture_url"].as<std::string>()
                };
            } catch (const pqxx::sql_error &e) {
                std::cerr << "SQL error: " << e.what() << " Query: " << e.query() << '\n';
                return std::nullopt;
            } catch (const std::exception &e) {
                std::cerr << "General error: " << e.what() << '\n';
                return std::nullopt;
            }
        });

        co_return authUser;
    }

    boost::asio::awaitable<std::optional<std::tuple<Models::DocumentFragment, Models::DocumentFragment>>> Database::selectTwoDocumentFragments(
        std::string_view firstDocId, std::string_view secondDocId, std::string_view fragmentName) {
        auto fragment = co_await Util::Async::AsyncExecute(pool_, [this, fragmentName, firstDocId, secondDocId] mutable -> std::optional<std::tuple<Models::DocumentFragment, Models::DocumentFragment>> {
            try {
                auto connection = getConnection();
                pqxx::work txn(*connection);
                std::string selectFragmentSql = R"(
                    SELECT document_sections.id , d.id, d.external_id FROM document_sections
                    JOIN public.documents d on d.id = document_sections.document_id
                    WHERE
                        document_sections.title = $1
                        AND d.external_id IN($2, $3)
                )";

                auto result= txn.exec(
                    selectFragmentSql,
                    pqxx::params{
                        fragmentName,
                        firstDocId,
                        secondDocId,
                    }
                );

                if (result.size() != 2) {
                    for (auto const &row : result) {
                        for (auto const &field : row) {
                            std::cout << field.c_str() << "\t";
                        }
                        std::cout << std::endl;
                    }
                    return std::nullopt;
                }

                Models::DocumentFragment firstFragment;
                Models::DocumentFragment secondFragment;

                for (const auto& row : result) {
                    Models::DocumentFragment currentFragment;

                    currentFragment.fragment_id = row[0].as<std::string>();
                    currentFragment.document_id = row[1].as<std::string>();
                    std::string external_id = row[2].as<std::string>();

                    if (external_id == firstDocId) {
                        firstFragment = std::move(currentFragment);
                    } else if (external_id == secondDocId) {
                        secondFragment = std::move(currentFragment);
                    }
                }

                return std::make_tuple(firstFragment, secondFragment);

            } catch (const pqxx::sql_error &e) {
                std::cerr << "SQL error: " << e.what() << " Query: " << e.query() << '\n';
                return std::nullopt;
            } catch (const std::exception &e) {
                std::cerr << "General error: " << e.what() << '\n';
                return std::nullopt;
            }
        });

        co_return fragment;
    }

    std::unique_ptr<pqxx::connection> Database::getConnection() {
        return std::make_unique<pqxx::connection>(connectionString_);
    }
}
