#include "pugixml.hpp"
#include "DocumentReader/Walker.hpp"

#include <gtest/gtest.h>

#include <mz.h>
#include <mz_strm.h>
#include <mz_strm_mem.h>
#include <mz_zip.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class DocReaderFixture : public testing::Test {
protected:
    std::string documentXml;
    std::string trueDocumentXml;
    std::string newDocumentXml;
    std::string pz1DocumentXml;
    std::string pz2DocumentXml;

    void SetUp() override {
        const auto docxPath = std::filesystem::path(TEST_DOC_DIR) / "ParsTest.docx";
        documentXml = readDocumentXml(docxPath);

        auto trueDocxPath = std::filesystem::path(TEST_DOC_DIR) / "PZ.docx";
        if (!std::filesystem::exists(trueDocxPath)) {
            trueDocxPath = std::filesystem::path(TEST_DOC_DIR) / "PZ2.docx";
        }
        trueDocumentXml = readDocumentXml(trueDocxPath);

        const auto newDocumentXmlPath = std::filesystem::path(TEST_DOC_DIR) / "PZ_new" / "word" / "document.xml";
        newDocumentXml = readTextFile(newDocumentXmlPath);

        const auto pz1DocxPath = std::filesystem::path(TEST_DOC_DIR) / "PZ1.docx";
        pz1DocumentXml = readDocumentXml(pz1DocxPath);

        const auto pz2DocxPath = std::filesystem::path(TEST_DOC_DIR) / "PZ2.docx";
        pz2DocumentXml = readDocumentXml(pz2DocxPath);
    }

    void TearDown() override {}

    static void writeParagraphsToFiles(const std::vector<Documents::Paragraph>& paragraphs,
                                       const std::filesystem::path& outputDir) {
        std::filesystem::remove_all(outputDir);
        std::filesystem::create_directories(outputDir);
        std::unordered_map<std::string, std::size_t> usedFilenames;

        for (std::size_t i = 0; i < paragraphs.size(); ++i) {
            const auto& paragraph = paragraphs[i];
            const auto filename = makeUniqueParagraphFilename(paragraph.title, usedFilenames);
            std::ofstream file(outputDir / filename, std::ios::binary);
            if (!file) {
                throw std::runtime_error("Failed to create output file: " + (outputDir / filename).string());
            }

            file << paragraph.text;
        }
    }

    static std::string makeUniqueParagraphFilename(std::string_view title, std::unordered_map<std::string, std::size_t>& usedFilenames) {
        const auto filename = makeParagraphFilename(title);
        auto& count = usedFilenames[filename];
        ++count;

        if (count == 1) {
            return filename;
        }

        const auto extension = std::string_view(".txt");
        const auto stem = std::string_view(filename).substr(0, filename.size() - extension.size());
        return std::string(stem) + "_" + std::to_string(count) + std::string(extension);
    }

    static std::string makeParagraphFilename(std::string_view title) {
        title = stripLeadingSectionNumber(title);
        std::string filename;
        bool previousUnderscore = false;

        for (const unsigned char c : title) {
            const bool forbiddenAscii = c < 32 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
                                        c == '<' || c == '>' || c == '|';
            const bool asciiSpace = c == ' ' || c == '\t' || c == '\n' || c == '\r';

            if (forbiddenAscii || asciiSpace) {
                if (!previousUnderscore) {
                    filename += '_';
                    previousUnderscore = true;
                }
                continue;
            }

            filename += static_cast<char>(c);
            previousUnderscore = false;
        }

        while (!filename.empty() && filename.back() == '_') {
            filename.pop_back();
        }

        if (filename.empty()) {
            filename = "untitled";
        }

        filename += ".txt";
        return filename;
    }

    static std::string_view stripLeadingSectionNumber(std::string_view title) {
        title = trimAscii(title);
        std::size_t i = 0;
        bool hasDigit = false;

        while (i < title.size() && title[i] >= '0' && title[i] <= '9') {
            hasDigit = true;
            ++i;
        }

        if (!hasDigit) {
            return title;
        }

        while (i < title.size() && title[i] == '.') {
            ++i;
            bool hasNestedDigit = false;
            while (i < title.size() && title[i] >= '0' && title[i] <= '9') {
                hasNestedDigit = true;
                ++i;
            }
            if (!hasNestedDigit) {
                return title;
            }
        }

        if (i >= title.size() || !isAsciiSpace(title[i])) {
            return title;
        }

        while (i < title.size() && isAsciiSpace(title[i])) {
            ++i;
        }

        return title.substr(i);
    }

    static std::string_view trimAscii(std::string_view text) {
        while (!text.empty() && isAsciiSpace(text.front())) {
            text.remove_prefix(1);
        }

        while (!text.empty() && isAsciiSpace(text.back())) {
            text.remove_suffix(1);
        }

        return text;
    }

    static bool isAsciiSpace(char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    }

    static std::vector<Documents::Paragraph> splitDocument(std::string_view documentXml) {
        pugi::xml_document doc;
        const std::string xml(documentXml);
        const auto parseResult = doc.load_string(xml.c_str(), pugi::parse_default | pugi::parse_ws_pcdata);
        if (!parseResult) {
            throw std::runtime_error("Failed to parse document XML");
        }

        Walker::SegmentDocWalker walker;
        doc.traverse(walker);
        return walker.result;
    }

private:
    static std::vector<unsigned char> readFile(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Failed to open DOCX test file: " + path.string());
        }

        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    static std::string readTextFile(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Failed to open XML test file: " + path.string());
        }

        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    static std::string readDocumentXml(const std::filesystem::path& path) {
        auto docx = readFile(path);

        void* memStream = mz_stream_mem_create();
        void* zipHandle = nullptr;

        mz_stream_mem_set_buffer(memStream, docx.data(), docx.size());
        if (mz_stream_open(memStream, nullptr, MZ_OPEN_MODE_READ) != MZ_OK) {
            mz_stream_mem_delete(&memStream);
            throw std::runtime_error("Failed to open DOCX memory stream");
        }

        zipHandle = mz_zip_create();
        if (!zipHandle) {
            mz_stream_close(memStream);
            mz_stream_mem_delete(&memStream);
            throw std::runtime_error("Failed to create DOCX ZIP handle");
        }

        if (mz_zip_open(zipHandle, memStream, MZ_OPEN_MODE_READ) != MZ_OK) {
            mz_zip_delete(&zipHandle);
            mz_stream_close(memStream);
            mz_stream_mem_delete(&memStream);
            throw std::runtime_error("Failed to open DOCX ZIP");
        }

        if (mz_zip_locate_entry(zipHandle, "word/document.xml", 0) != MZ_OK) {
            mz_zip_close(zipHandle);
            mz_zip_delete(&zipHandle);
            mz_stream_close(memStream);
            mz_stream_mem_delete(&memStream);
            throw std::runtime_error("word/document.xml was not found in DOCX");
        }

        if (mz_zip_entry_read_open(zipHandle, 0, nullptr) != MZ_OK) {
            mz_zip_close(zipHandle);
            mz_zip_delete(&zipHandle);
            mz_stream_close(memStream);
            mz_stream_mem_delete(&memStream);
            throw std::runtime_error("Failed to open word/document.xml");
        }

        std::vector<std::uint8_t> output(8192);
        int64_t total = 0;
        int32_t read = 0;
        while ((read = mz_zip_entry_read(zipHandle, output.data() + total, output.size() - total)) > 0) {
            total += read;
            if (total >= static_cast<int64_t>(output.size())) {
                output.resize(output.size() * 2);
            }
        }

        if (read < 0) {
            mz_zip_entry_close(zipHandle);
            mz_zip_close(zipHandle);
            mz_zip_delete(&zipHandle);
            mz_stream_close(memStream);
            mz_stream_mem_delete(&memStream);
            throw std::runtime_error("Failed to read word/document.xml");
        }

        output.resize(static_cast<std::size_t>(total));

        mz_zip_entry_close(zipHandle);
        mz_zip_close(zipHandle);
        mz_zip_delete(&zipHandle);
        mz_stream_close(memStream);
        mz_stream_mem_delete(&memStream);

        return {output.begin(), output.end()};
    }
};

TEST_F(DocReaderFixture, StoresDocumentXml) {
    ASSERT_FALSE(documentXml.empty());
    EXPECT_NE(documentXml.find("<w:document"), std::string::npos);
    EXPECT_NE(documentXml.find("<w:body>"), std::string::npos);
}

TEST_F(DocReaderFixture, StoresTrueDocumentXml) {
    ASSERT_FALSE(trueDocumentXml.empty());
}

TEST_F(DocReaderFixture, StoresNewDocumentXml) {
    ASSERT_FALSE(newDocumentXml.empty());
    EXPECT_NE(newDocumentXml.find("<w:document"), std::string::npos);
}

TEST_F(DocReaderFixture, StoresPz1AndPz2DocumentXml) {
    ASSERT_FALSE(pz1DocumentXml.empty());
    ASSERT_FALSE(pz2DocumentXml.empty());
}

TEST_F(DocReaderFixture, SplitDocumentTest) {
    ASSERT_FALSE(documentXml.empty());

    pugi::xml_document doc;
    ASSERT_TRUE(doc.load_string(documentXml.c_str(), pugi::parse_default | pugi::parse_ws_pcdata));
    Walker::SegmentDocWalker walker;
    doc.traverse(walker);
    auto res = walker.result;

    ASSERT_EQ(res.size(), 3);
    EXPECT_EQ(res[0].title, "1 ПОСТРОЕНИЕ ИНФОЛОГИЧЕСКОЙ КОНЦЕПТУАЛЬНОЙ МОДЕЛИ  ");
    EXPECT_TRUE(res[0].text.empty());
    EXPECT_EQ(res[1].title, "2 ПОСТРОЕНИЕ СХЕМЫ РЕЛЯЦИОННОЙ БАЗЫ ДАННЫХ");
    EXPECT_TRUE(res[1].text.empty());
    EXPECT_EQ(res[2].title, "3 СОЗДАНИЕ СПРОЕКТИРОВАННОЙ БАЗЫ ДАННЫХ");
    EXPECT_TRUE(res[2].text.empty());
}

TEST_F(DocReaderFixture, SplitTrueDocumentTest) {
    ASSERT_FALSE(trueDocumentXml.empty());
    pugi::xml_document doc;
    ASSERT_TRUE(doc.load_string(trueDocumentXml.c_str(), pugi::parse_default | pugi::parse_ws_pcdata));
    Walker::SegmentDocWalker walker;
    doc.traverse(walker);
    auto res = walker.result;

    ASSERT_FALSE(res.empty());

    const auto outputDir = std::filesystem::path(TEST_OUTPUT_DIR) / "SplitTrueDocumentTest";
    writeParagraphsToFiles(res, outputDir);
    EXPECT_TRUE(std::filesystem::exists(outputDir / makeParagraphFilename(res[0].title)));
}

TEST_F(DocReaderFixture, SplitNewDocumentTest) {
    ASSERT_FALSE(newDocumentXml.empty());

    pugi::xml_document doc;
    ASSERT_TRUE(doc.load_string(newDocumentXml.c_str(), pugi::parse_default | pugi::parse_ws_pcdata));
    Walker::SegmentDocWalker walker;
    doc.traverse(walker);
    auto res = walker.result;

    ASSERT_GE(res.size(), 9);
    EXPECT_EQ(res[0].title, "ВВЕДЕНИЕ");
    EXPECT_EQ(res[1].title, "1 Построение инфологической концептуальной модели ");
    EXPECT_EQ(res[2].title, "2 Построение схемы реляционной базы данных");
    EXPECT_EQ(res[3].title, "3 Создание спроектированной базы данных  ");
}

TEST_F(DocReaderFixture, ComparePz1AndPz2SectionsTest) {
    ASSERT_FALSE(pz1DocumentXml.empty());
    ASSERT_FALSE(pz2DocumentXml.empty());

    const auto pz1Sections = splitDocument(pz1DocumentXml);
    const auto pz2Sections = splitDocument(pz2DocumentXml);

    ASSERT_EQ(pz1Sections.size(), pz2Sections.size());

    for (std::size_t i = 0; i < pz1Sections.size(); ++i) {
        SCOPED_TRACE("section index: " + std::to_string(i));
        EXPECT_EQ(pz1Sections[i].title, pz2Sections[i].title);
        EXPECT_FALSE(pz1Sections[i].text.empty());
        EXPECT_FALSE(pz2Sections[i].text.empty());
    }
}
