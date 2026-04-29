#include "DocReader.hpp"

#include "Walker.hpp"
#include "PdfDocumentFromMemory.hpp"
#include <poppler/cpp/poppler-document.h>
#include <poppler/cpp/poppler-page.h>

#include <pugixml.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <mz.h>
#include <mz_strm.h>
#include <mz_strm_mem.h>
#include <mz_zip.h>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
std::optional<int> parseInt(std::string_view text) {
    int value = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc() || ptr != end) {
        return std::nullopt;
    }

    return value;
}

std::string asciiLower(std::string_view text) {
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return result;
}

std::string trimAscii(std::string text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\n' || text.front() == '\r')) {
        text.erase(text.begin());
    }

    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\n' || text.back() == '\r')) {
        text.pop_back();
    }

    return text;
}

void appendParagraphText(pugi::xml_node node, std::string& text) {
    if (std::string_view(node.name()) == "w:t") {
        text += node.text().as_string();
    }

    for (const auto child : node.children()) {
        appendParagraphText(child, text);
    }
}

std::string paragraphText(pugi::xml_node paragraph) {
    std::string text;
    appendParagraphText(paragraph, text);
    return text;
}

void eraseAll(std::string& text, std::string_view token) {
    std::size_t pos = 0;
    while ((pos = text.find(token, pos)) != std::string::npos) {
        text.erase(pos, token.size());
    }
}

void replaceAll(std::string& text, std::string_view token, char replacement) {
    std::size_t pos = 0;
    while ((pos = text.find(token, pos)) != std::string::npos) {
        text.replace(pos, token.size(), 1, replacement);
        ++pos;
    }
}

bool isStandardTableOfContents(pugi::xml_node node) {
    const auto gallery = node.child("w:sdtPr").child("w:docPartObj").child("w:docPartGallery");
    return std::string_view(gallery.attribute("w:val").value()) == "Table of Contents";
}

bool hasDotLeaderTab(pugi::xml_node paragraph) {
    const auto tabs = paragraph.child("w:pPr").child("w:tabs");
    for (const auto tab : tabs.children("w:tab")) {
        if (std::string_view(tab.attribute("w:leader").value()) == "dot") {
            return true;
        }
    }

    return false;
}

std::optional<int> numberingLevel(pugi::xml_node paragraph) {
    const std::string_view level = paragraph.child("w:pPr").child("w:numPr").child("w:ilvl").attribute("w:val").value();
    if (level.empty()) {
        return std::nullopt;
    }

    return parseInt(level);
}

std::optional<int> tocLevelFromStyle(std::string_view styleId, const DocReader::DocumentStyles& styles) {
    if (styleId.empty()) {
        return std::nullopt;
    }

    const auto lowerId = asciiLower(styleId);
    if (lowerId == "toc1") {
        return 1;
    }

    const auto styleIt = styles.styles.find(std::string(styleId));
    if (styleIt == styles.styles.end()) {
        return std::nullopt;
    }

    const auto lowerName = asciiLower(styleIt->second.name);
    constexpr std::string_view tocPrefix = "toc ";
    if (!lowerName.starts_with(tocPrefix)) {
        return std::nullopt;
    }

    return parseInt(std::string_view(lowerName).substr(tocPrefix.size()));
}

bool isTopLevelTocParagraph(pugi::xml_node paragraph, const DocReader::DocumentStyles& styles) {
    const std::string_view style = paragraph.child("w:pPr").child("w:pStyle").attribute("w:val").value();
    const auto tocLevel = tocLevelFromStyle(style, styles);
    if (tocLevel.has_value()) {
        return *tocLevel == 1;
    }

    const auto level = numberingLevel(paragraph);
    if (level.has_value()) {
        return *level == 0;
    }

    return true;
}

bool textHasDotLeaders(std::string_view text) {
    return text.find("...") != std::string_view::npos || text.find("…") != std::string_view::npos;
}

std::string cleanTocTitle(std::string text) {
    eraseAll(text, "\xE2\x80\x8B");
    eraseAll(text, "\xEF\xBB\xBF");
    replaceAll(text, "\xC2\xA0", ' ');

    for (auto& c : text) {
        if (c == '\t' || c == '\n' || c == '\r') {
            c = ' ';
        }
    }

    text = trimAscii(text);

    while (!text.empty() && text.back() >= '0' && text.back() <= '9') {
        text.pop_back();
    }

    return trimAscii(text);
}

bool isManualTocHeading(std::string_view text) {
    return text == "СОДЕРЖАНИЕ" || text == "Содержание" || text == "ОГЛАВЛЕНИЕ" || text == "Оглавление";
}

struct TocCollectionState {
    bool manualTocStarted = false;
    bool manualTocFinished = false;
    bool collectedManualTocRows = false;
};

void collectTopLevelTocTitles(pugi::xml_node node,
                              bool insideStandardToc,
                              const DocReader::DocumentStyles& styles,
                              TocCollectionState& state,
                              std::vector<std::string>& titles) {
    if (std::string_view(node.name()) == "w:sdt" && isStandardTableOfContents(node)) {
        insideStandardToc = true;
    }

    if (std::string_view(node.name()) == "w:p") {
        auto text = cleanTocTitle(paragraphText(node));
        if (text.empty()) {
            return;
        }

        if (insideStandardToc) {
            const std::string_view style = node.child("w:pPr").child("w:pStyle").attribute("w:val").value();
            const auto tocLevel = tocLevelFromStyle(style, styles);
            if (tocLevel.has_value() && *tocLevel == 1) {
                titles.emplace_back(std::move(text));
            }
            return;
        }

        if (!state.manualTocStarted && isManualTocHeading(text)) {
            state.manualTocStarted = true;
            return;
        }

        if (state.manualTocStarted && !state.manualTocFinished) {
            const bool isTocRow = hasDotLeaderTab(node) || textHasDotLeaders(text);
            if (isTocRow) {
                state.collectedManualTocRows = true;
                if (isTopLevelTocParagraph(node, styles)) {
                    titles.emplace_back(std::move(text));
                }
                return;
            }

            if (state.collectedManualTocRows) {
                state.manualTocFinished = true;
            }
        }
    }

    for (const auto child : node.children()) {
        collectTopLevelTocTitles(child, insideStandardToc, styles, state, titles);
    }
}

std::vector<std::string> extractTopLevelTocTitles(std::string_view documentXml, const DocReader::DocumentStyles& styles) {
    pugi::xml_document doc;
    const std::string xml(documentXml);
    if (!doc.load_string(xml.c_str(), pugi::parse_default | pugi::parse_ws_pcdata)) {
        return {};
    }

    std::vector<std::string> titles;
    TocCollectionState state;
    collectTopLevelTocTitles(doc, false, styles, state, titles);
    return titles;
}

std::optional<std::string> readZipEntry(void* zipHandle, const char* entryName) {
    if (mz_zip_locate_entry(zipHandle, entryName, 0) != MZ_OK) {
        return std::nullopt;
    }

    if (mz_zip_entry_read_open(zipHandle, 0, nullptr) != MZ_OK) {
        throw std::runtime_error(std::string("Failed to open entry for reading: ") + entryName);
    }

    std::vector<std::uint8_t> output(65536);
    int64_t total = 0;
    int32_t read = 0;
    while ((read = mz_zip_entry_read(zipHandle, output.data() + total, output.size() - total)) > 0) {
        total += read;
        if (total >= static_cast<int64_t>(output.size())) {
            output.resize(output.size() * 2);
        }
    }

    mz_zip_entry_close(zipHandle);

    if (read < 0) {
        throw std::runtime_error(std::string("Failed to read entry: ") + entryName);
    }

    output.resize(static_cast<std::size_t>(total));
    return std::string(output.begin(), output.end());
}
}  // namespace

std::optional<std::vector<Documents::Paragraph>> DocReader::zipReader(std::span<unsigned char> zip) {
    void* memStream = nullptr;
    void* zipHandle = nullptr;
    bool zipOpened = false;

    auto cleanup = [&] {
        if (zipHandle) {
            if (zipOpened) {
                mz_zip_close(zipHandle);
            }
            mz_zip_delete(&zipHandle);
        }
        if (memStream) {
            mz_stream_close(memStream);
            mz_stream_mem_delete(&memStream);
        }
    };

    memStream = mz_stream_mem_create();
    mz_stream_mem_set_buffer(memStream, zip.data(), zip.size());

    if (mz_stream_open(memStream, nullptr, MZ_OPEN_MODE_READ) != MZ_OK) {
        mz_stream_mem_delete(&memStream);
        throw std::runtime_error("Failed to set memory buffer");
    }

    zipHandle = mz_zip_create();
    if (!zipHandle) {
        cleanup();
        throw std::runtime_error("Failed to create ZIP handle");
    }

    if (mz_zip_open(zipHandle, memStream, MZ_OPEN_MODE_READ) != MZ_OK) {
        cleanup();
        throw std::runtime_error("Failed to open zip");
    }
    zipOpened = true;

    try {
        auto documentXml = readZipEntry(zipHandle, "word/document.xml");
        if (!documentXml.has_value()) {
            cleanup();
            return std::nullopt;
        }

        auto stylesXml = readZipEntry(zipHandle, "word/styles.xml").value_or("");
        cleanup();

        auto styles = parseStyles(stylesXml);
        auto topLevelTocTitles = extractTopLevelTocTitles(*documentXml, styles);
        return DocxReader(*documentXml, styles, std::move(topLevelTocTitles));
    } catch (...) {
        cleanup();
        throw;
    }
}

std::vector<std::string> DocReader::splitText(const std::string& text) { return std::vector<std::string>(); }

std::vector<Documents::Paragraph> DocReader::DocxReader(const std::string_view xml) {
    pugi::xml_document doc;
    const std::string documentXml(xml);
    doc.load_string(documentXml.c_str(), pugi::parse_default | pugi::parse_ws_pcdata);
    Walker::SegmentDocWalker walker;
    doc.traverse(walker);
    return walker.result;
}

std::vector<Documents::Paragraph> DocReader::DocxReader(const std::string_view xml,
                                                        const DocumentStyles& styles,
                                                        std::vector<std::string> topLevelTocTitles) {
    pugi::xml_document doc;
    const std::string documentXml(xml);
    doc.load_string(documentXml.c_str(), pugi::parse_default | pugi::parse_ws_pcdata);

    auto topLevelHeadingStyles = styles.hasStyleDefinitions ? styles.topLevelHeadingStyles
                                                            : Walker::SegmentDocWalker::defaultTopLevelHeadingStyles();
    const bool allowTocTitleMatching = topLevelHeadingStyles.empty() && !topLevelTocTitles.empty();
    Walker::SegmentDocWalker walker(std::move(topLevelHeadingStyles), std::move(topLevelTocTitles), allowTocTitleMatching);
    doc.traverse(walker);
    return walker.result;
}

std::vector<Documents::Paragraph> DocReader::PdfReader(std::span<unsigned char> pdf) {
    auto rawDocument = PdfDocumentFromMemory(pdf);

    auto& document = rawDocument.document();
    Walker::SegmentPdfWalker walker;

    for (int i = 0; i < document.pages(); i++) {
        std::unique_ptr<poppler::page> page(document.create_page(i));
        if (!page) {
            continue;
        }

        auto utf_8 = page->text().to_utf8();

        std::string text(utf_8.data(), utf_8.size());
        walker.pushText(text);
    }

    walker.finish();
    return walker.result;
}
