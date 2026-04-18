#include "DocumentReader/Walker.hpp"

#include <gtest/gtest.h>

TEST(First, FirstTest) { EXPECT_EQ(1, 1); }


TEST(WalkerTest, SegmentDocWalkerTest) {
    std::string str = R"(
        <w:document>
            <w:body>
                <w:p><w:r><w:t>1 ПОСТРОЕНИЕ ИНФОЛОГИЧЕСКОЙ КОНЦЕПТУАЛЬНОЙ МОДЕЛИ</w:t></w:r></w:p>
                <w:p><w:r><w:t>обычный текст между заголовками</w:t></w:r></w:p>
                <w:p><w:r><w:t>2 ПОСТРОЕНИЕ СХЕМЫ РЕЛЯЦИОННОЙ БАЗЫ ДАННЫХ</w:t></w:r></w:p>
                <w:p><w:r><w:t>длфвоаыдвалопывдалоп</w:t></w:r></w:p>
                <w:p><w:r><w:t>длывфоадфлыофдывла фы влдаод фывад лфовыда лождывло аджфлыо</w:t></w:r></w:p>
                <w:p><w:r><w:t>3 СОЗДАНИЕ СПРОЕКТИРОВАННОЙ БАЗЫ ДАННЫХ</w:t></w:r></w:p>
                <w:p><w:r><w:t>конец документа</w:t></w:r></w:p>
            </w:body>
        </w:document>
    )";

    Walker::SegmentDocWalker walker;
    pugi::xml_document doc;
    ASSERT_TRUE(doc.load_string(str.c_str(), pugi::parse_default | pugi::parse_ws_pcdata));
    doc.traverse(walker);
    auto res = walker.result;

    EXPECT_EQ(res.size(), 3);
    EXPECT_EQ(res[0].title, "построение инфологической концептуальной модели");
    EXPECT_EQ(res[0].text, "обычный текст между заголовками");
    EXPECT_EQ(res[1].title, "построение схемы реляционной базы данных");
    EXPECT_EQ(res[1].text,
              "длфвоаыдвалопывдалоп\n"
              "длывфоадфлыофдывла фы влдаод фывад лфовыда лождывло аджфлыо");
    EXPECT_EQ(res[2].title, "создание спроектированной базы данных");
    EXPECT_EQ(res[2].text, "конец документа");

}

TEST(WalkerTest, SegmentDocWalkerSkipsTableOfContents) {
    std::string str = R"(
        <w:document>
            <w:body>
                <w:sdt>
                    <w:sdtPr>
                        <w:docPartObj>
                            <w:docPartGallery w:val="Table of Contents" />
                        </w:docPartObj>
                    </w:sdtPr>
                    <w:sdtContent>
                        <w:p><w:r><w:t>1 ПЕРВАЯ ГЛАВА</w:t></w:r></w:p>
                        <w:p><w:r><w:t>2 ВТОРАЯ ГЛАВА</w:t></w:r></w:p>
                    </w:sdtContent>
                </w:sdt>
                <w:p><w:r><w:t>1 ПЕРВАЯ ГЛАВА</w:t></w:r></w:p>
                <w:p><w:r><w:t>Текст первой главы</w:t></w:r></w:p>
                <w:p><w:r><w:t>2 ВТОРАЯ ГЛАВА</w:t></w:r></w:p>
                <w:p><w:r><w:t>Текст второй главы</w:t></w:r></w:p>
            </w:body>
        </w:document>
    )";

    Walker::SegmentDocWalker walker;
    pugi::xml_document doc;
    ASSERT_TRUE(doc.load_string(str.c_str(), pugi::parse_default | pugi::parse_ws_pcdata));
    doc.traverse(walker);
    auto res = walker.result;

    ASSERT_EQ(res.size(), 2);
    EXPECT_EQ(res[0].title, "первая глава");
    EXPECT_EQ(res[0].text, "Текст первой главы");
    EXPECT_EQ(res[1].title, "вторая глава");
    EXPECT_EQ(res[1].text, "Текст второй главы");
}

TEST(WalkerTest, SegmentDocWalkerSkipsCourierNewRuns) {
    std::string str = R"(
        <w:document>
            <w:body>
                <w:p><w:r><w:t>1 ПЕРВАЯ ГЛАВА</w:t></w:r></w:p>
                <w:p>
                    <w:r>
                        <w:rPr><w:rFonts w:cs="Courier New" /></w:rPr>
                        <w:t>служебный текст</w:t>
                    </w:r>
                    <w:r><w:t>Основной текст</w:t></w:r>
                </w:p>
            </w:body>
        </w:document>
    )";

    Walker::SegmentDocWalker walker;
    pugi::xml_document doc;
    ASSERT_TRUE(doc.load_string(str.c_str(), pugi::parse_default | pugi::parse_ws_pcdata));
    doc.traverse(walker);
    auto res = walker.result;

    ASSERT_EQ(res.size(), 1);
    EXPECT_EQ(res[0].title, "первая глава");
    EXPECT_EQ(res[0].text, "Основной текст");
}
