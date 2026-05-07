#include <gtest/gtest.h>
#include "../../src/Models/Paragraph.hpp"

#include "Session/DataBaseSession.hpp"

#include <poppler-document.h>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>

class FixtureDatabase : public testing::Test {
protected:
    void SetUp() override {
        database = std::make_shared<Network::DataBaseSession>();
    }

public:
    std::shared_ptr<Network::DataBaseSession> database;
};


TEST_F(FixtureDatabase, InitTest) {
    ASSERT_TRUE(database);
}

TEST_F(FixtureDatabase, InsertTest) {
    ASSERT_TRUE(database);

    std::vector<Documents::Paragraph> paragraphs{
        {
            .title = "Введение",
            .text = "Текст первого раздела тестового документа.",
        },
        {
            .title = "Заключение",
            .text = "Текст второго раздела тестового документа.",
        },
    };

    Document document(std::move(paragraphs), "test-external-document-id");

    boost::asio::io_context io;
    auto insertResult = boost::asio::co_spawn(
        io,
        database->insertDocument(document),
        boost::asio::use_future
    );

    io.run();

    ASSERT_TRUE(insertResult.get());
}

TEST_F(FixtureDatabase, SelectDocument) {
    ASSERT_TRUE(database);
    boost::asio::io_context io;

    auto selectResultFuture = boost::asio::co_spawn(
      io,
      database->selectDocumentById("test-external-document-id"),
      boost::asio::use_future
    );

    io.run();

    auto selectResult = selectResultFuture.get();

    ASSERT_TRUE(selectResult.has_value());

    auto&& document = selectResult.value();

    EXPECT_EQ("test-external-document-id", document.docId);

    auto&& documentText = document.text;

    EXPECT_EQ("Введение", documentText[0].title);
    EXPECT_EQ("Заключение", documentText[1].title);

    EXPECT_EQ("Текст первого раздела тестового документа.", documentText[0].text);
    EXPECT_EQ("Текст второго раздела тестового документа.", documentText[1].text);
}

TEST_F(FixtureDatabase, DeleteDocument) {
    ASSERT_TRUE(database);

    boost::asio::io_context io;
    auto deleteResult = boost::asio::co_spawn(
      io,
      database->deleteDocument("test-external-document-id"),
      boost::asio::use_future
    );

    io.run();
    ASSERT_TRUE(deleteResult.get());
}
