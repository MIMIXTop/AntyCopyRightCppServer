#include "Walker.hpp"

#include <cstdint>
#include <utility>
#include <unicode/uchar.h>
#include <unicode/utf8.h>
#include <unicode/uscript.h>

namespace Walker {
SegmentDocWalker::SegmentDocWalker()
  : SegmentDocWalker(defaultTopLevelHeadingStyles(), {}) {}

SegmentDocWalker::SegmentDocWalker(std::unordered_set<std::string> topLevelHeadingStyles,
                                   std::vector<std::string> topLevelTocTitles,
                                   bool allowTocTitleMatching)
  : topLevelHeadingStyles(std::move(topLevelHeadingStyles)), allowTocTitleMatching(allowTocTitleMatching) {
    normalizedTopLevelTocTitles.reserve(topLevelTocTitles.size());
    for (const auto& title : topLevelTocTitles) {
        auto normalizedTitle = normalizeHeadingForMatch(title);
        if (!normalizedTitle.empty()) {
            normalizedTopLevelTocTitles.emplace_back(std::move(normalizedTitle));
        }
    }
}

std::unordered_set<std::string> SegmentDocWalker::defaultTopLevelHeadingStyles() {
    return { "Heading1", "1654", "1702" };
}

bool SegmentDocWalker::for_each(pugi::xml_node& node) {
    std::string_view name = node.name();

    if (name == "w:p") {
        flushParagraph();
        paragraphStyle = node.child("w:pPr").child("w:pStyle").attribute("w:val").value();
        paragraphHasDotLeaderTab = hasDotLeaderTab(node);
    }

    if (name == "w:r") {
        skipCurrentRunText = false;
    }

    if (name == "w:rFonts" && hasCourierNewFont(node)) {
        skipCurrentRunText = true;
    }

    if (name == "w:bookmarkStart" && !isInsideTableOfContents(node)) {
        const std::string_view bookmarkName = node.attribute("w:name").value();
        paragraphHasTocBookmark = paragraphHasTocBookmark || bookmarkName.starts_with("_Toc");
    }

    if (name == "w:t" && !skipCurrentRunText && !isInsideTableOfContents(node)) {
        std::string_view text = node.text().as_string();
        const auto check = checkText(text);
        paragraphText += text;
        paragraphHasLetter = paragraphHasLetter || check.hasLetter;
        paragraphHasCyrillic = paragraphHasCyrillic || check.hasCyrillic;
        paragraphIsUpperCase = paragraphIsUpperCase && check.isUpperCaseCompatible;
    }

    return true;
}

bool SegmentDocWalker::end(pugi::xml_node& node) {
    flushParagraph();
    flushSection();
    return true;
}

void SegmentDocWalker::flushParagraph() {
    if (!paragraphText.empty()) {
        const bool isHeading = !looksLikeManualTocRow() && paragraphHasLetter && paragraphHasCyrillic && isHeadingParagraph();
        if (isHeading) {
            flushSection();
            currentSection.title = normalizeTitle(paragraphText);
        } else if (!looksLikeManualTocRow() && !currentSection.title.empty()) {
            if (!currentSection.text.empty()) {
                currentSection.text += '\n';
            }
            currentSection.text += paragraphText;
        }
    }

    paragraphText.clear();
    paragraphStyle.clear();
    paragraphHasLetter = false;
    paragraphHasCyrillic = false;
    paragraphIsUpperCase = true;
    paragraphHasTocBookmark = false;
    paragraphHasDotLeaderTab = false;
    skipCurrentRunText = false;
}

void SegmentDocWalker::flushSection() {
    if (!currentSection.title.empty()) {
        result.emplace_back(currentSection);
        currentSection = {};
    }
}

bool SegmentDocWalker::isHeadingParagraph() const {
    if (isHeadingStyle(paragraphStyle)) {
        return true;
    }

    if (allowTocTitleMatching && matchesTopLevelTocTitle(paragraphText)) {
        return true;
    }

    if (paragraphHasTocBookmark && looksLikeHeadingText(paragraphText)) {
        return true;
    }

    return paragraphIsUpperCase && looksLikeTopLevelHeadingText(paragraphText);
}

bool SegmentDocWalker::isHeadingStyle(std::string_view style) const {
    return !style.empty() && topLevelHeadingStyles.contains(std::string(style));
}

bool SegmentDocWalker::looksLikeHeadingText(std::string_view text) {
    return looksLikeTopLevelHeadingText(text);
}

bool SegmentDocWalker::looksLikeTopLevelHeadingText(std::string_view text) {
    const auto trimmed = trim(text);
    if (trimmed.empty()) {
        return false;
    }

    return startsWithTopLevelNumberedHeading(trimmed) || startsWithAny(trimmed, {
               "ВВЕДЕНИЕ",
               "Введение",
               "ЗАКЛЮЧЕНИЕ",
               "Заключение",
               "СПИСОК",
               "Список",
               "ПРИЛОЖЕНИЕ",
               "Приложение",
           });
}

bool SegmentDocWalker::startsWithTopLevelNumberedHeading(std::string_view text) {
    std::size_t i = 0;
    bool hasDigit = false;

    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        hasDigit = true;
        ++i;
    }

    if (!hasDigit) {
        return false;
    }

    if (i < text.size() && text[i] == '.') {
        return false;
    }

    return i < text.size() && (text[i] == ' ' || text[i] == '\t');
}

bool SegmentDocWalker::startsWithAny(std::string_view text, std::initializer_list<std::string_view> prefixes) {
    for (const auto prefix : prefixes) {
        if (text.starts_with(prefix)) {
            return true;
        }
    }

    return false;
}

std::string_view SegmentDocWalker::trim(std::string_view text) {
    while (!text.empty() && isAsciiSpace(text.front())) {
        text.remove_prefix(1);
    }

    while (!text.empty() && isAsciiSpace(text.back())) {
        text.remove_suffix(1);
    }

    return text;
}

bool SegmentDocWalker::isAsciiSpace(char c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\v';
}

bool SegmentDocWalker::matchesTopLevelTocTitle(std::string_view text) const {
    if (normalizedTopLevelTocTitles.empty()) {
        return false;
    }

    const auto normalizedText = normalizeHeadingForMatch(text);
    if (normalizedText.empty()) {
        return false;
    }

    for (const auto& tocTitle : normalizedTopLevelTocTitles) {
        if (normalizedText == tocTitle) {
            return true;
        }

        if (tocTitle.starts_with(normalizedText) && tocTitle.size() > normalizedText.size() &&
            tocTitle[normalizedText.size()] == ' ') {
            return true;
        }
    }

    return false;
}

bool SegmentDocWalker::looksLikeManualTocRow() const {
    return paragraphHasDotLeaderTab || (hasDotLeaders(paragraphText) && endsWithPageNumber(paragraphText));
}

bool SegmentDocWalker::hasDotLeaderTab(pugi::xml_node paragraph) {
    const auto tabs = paragraph.child("w:pPr").child("w:tabs");
    for (const auto tab : tabs.children("w:tab")) {
        if (std::string_view(tab.attribute("w:leader").value()) == "dot") {
            return true;
        }
    }

    return false;
}

bool SegmentDocWalker::hasDotLeaders(std::string_view text) {
    return text.find("...") != std::string_view::npos || text.find("…") != std::string_view::npos;
}

bool SegmentDocWalker::endsWithPageNumber(std::string_view text) {
    text = trim(text);
    if (text.empty() || text.back() < '0' || text.back() > '9') {
        return false;
    }

    std::size_t i = text.size();
    while (i > 0 && text[i - 1] >= '0' && text[i - 1] <= '9') {
        --i;
    }

    return i > 0 && !isAsciiSpace(text[i - 1]);
}

std::string SegmentDocWalker::normalizeHeadingForMatch(std::string_view text) {
    std::string normalized;
    normalized.reserve(text.size());

    int32_t i = 0;
    const int32_t length = static_cast<int32_t>(text.length());
    const auto* data = reinterpret_cast<const uint8_t*>(text.data());
    bool previousSpace = true;

    while (i < length) {
        UChar32 ch = 0;
        U8_NEXT(data, i, length, ch);
        if (ch < 0) {
            continue;
        }

        if (ch == 0x200B || ch == 0xFEFF) {
            continue;
        }

        if (ch == 0x00A0 || u_isspace(ch) || u_ispunct(ch)) {
            if (!previousSpace) {
                normalized += ' ';
                previousSpace = true;
            }
            continue;
        }

        const auto lower = u_tolower(ch);
        char buffer[4];
        int32_t offset = 0;
        auto* output = reinterpret_cast<uint8_t*>(buffer);
        U8_APPEND_UNSAFE(output, offset, lower);
        normalized.append(buffer, static_cast<std::size_t>(offset));
        previousSpace = false;
    }

    while (!normalized.empty() && normalized.back() == ' ') {
        normalized.pop_back();
    }

    return stripLeadingTopLevelNumber(std::move(normalized));
}

std::string SegmentDocWalker::stripLeadingTopLevelNumber(std::string text) {
    std::size_t i = 0;
    bool hasDigit = false;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        hasDigit = true;
        ++i;
    }

    if (!hasDigit || i >= text.size() || text[i] != ' ') {
        return text;
    }

    text.erase(0, i + 1);
    return text;
}

std::string SegmentDocWalker::normalizeTitle(std::string_view text) {
    std::string normalized;
    normalized.reserve(text.size());

    int32_t i = 0;
    const int32_t length = static_cast<int32_t>(text.length());
    const auto* data = reinterpret_cast<const uint8_t*>(text.data());
    bool previousSpace = true;

    while (i < length) {
        UChar32 ch = 0;
        U8_NEXT(data, i, length, ch);
        if (ch < 0 || u_isdigit(ch)) {
            continue;
        }

        if (ch == 0x00A0 || u_isspace(ch)) {
            if (!previousSpace) {
                normalized += ' ';
                previousSpace = true;
            }
            continue;
        }

        ch = u_tolower(ch);
        char buffer[4];
        int32_t offset = 0;
        auto* output = reinterpret_cast<uint8_t*>(buffer);
        U8_APPEND_UNSAFE(output, offset, ch);
        normalized.append(buffer, static_cast<std::size_t>(offset));
        previousSpace = false;
    }

    while (!normalized.empty() && normalized.back() == ' ') {
        normalized.pop_back();
    }

    return normalized;
}

bool SegmentDocWalker::isInsideTableOfContents(pugi::xml_node node) {
    for (pugi::xml_node parent = node.parent(); parent; parent = parent.parent()) {
        if (std::string_view(parent.name()) != "w:sdt") {
            continue;
        }

        const auto gallery = parent.child("w:sdtPr").child("w:docPartObj").child("w:docPartGallery");
        if (std::string_view(gallery.attribute("w:val").value()) == "Table of Contents") {
            return true;
        }
    }

    return false;
}

bool SegmentDocWalker::hasCourierNewFont(pugi::xml_node node) {
    return isCourierNew(node.attribute("w:cs").value()) || isCourierNew(node.attribute("w:ascii").value()) ||
           isCourierNew(node.attribute("w:hAnsi").value());
}

bool SegmentDocWalker::isCourierNew(std::string_view font) {
    return font == "Courier New";
}

bool SegmentDocWalker::isCyrillic(UChar32 c) {
    UErrorCode status = U_ZERO_ERROR;
    const auto script = uscript_getScript(c, &status);
    return U_SUCCESS(status) && script == USCRIPT_CYRILLIC;
}

SegmentDocWalker::TextCheckResult SegmentDocWalker::checkText(std::string_view text) {
    TextCheckResult result;

    int32_t i = 0;
    const int32_t length = static_cast<int32_t>(text.length());
    const auto* data = reinterpret_cast<const uint8_t*>(text.data());
    while (i < length) {
        UChar32 ch = 0;
        U8_NEXT(data, i, length, ch);

        if (ch < 0) {
            result.isUpperCaseCompatible = false;
            return result;
        }

        if (u_isdigit(ch) || u_isspace(ch) || u_ispunct(ch)) {
            continue;
        }

        if (u_isalpha(ch)) {
            result.hasLetter = true;

            if (isCyrillic(ch)) {
                result.hasCyrillic = true;
            }

            if (!u_isupper(ch)) {
                result.isUpperCaseCompatible = false;
                return result;
            }
            continue;
        }
        result.isUpperCaseCompatible = false;
        return result;
    }
    return result;
}

void SegmentPdfWalker::pushText(std::string_view text) {
    std::size_t lineStart = 0;
    while (lineStart <= text.size()) {
        const auto lineEnd = text.find('\n', lineStart);
        const auto length = lineEnd == std::string_view::npos ? text.size() - lineStart : lineEnd - lineStart;
        pushParagraph(text.substr(lineStart, length));

        if (lineEnd == std::string_view::npos) {
            break;
        }
        lineStart = lineEnd + 1;
    }
}

void SegmentPdfWalker::finish() {
    flushSection();
}

void SegmentPdfWalker::pushParagraph(std::string_view text) {
    text = trim(text);
    if (text.empty()) {
        return;
    }

    const auto check = checkText(text);
    const bool isHeading = !looksLikeManualTocRow(text) && check.hasLetter && check.hasCyrillic &&
                           check.isUpperCaseCompatible && looksLikeTopLevelHeadingText(text);
    if (isHeading) {
        flushSection();
        currentSection.title = normalizeTitle(text);
        return;
    }

    if (!looksLikeManualTocRow(text) && !currentSection.title.empty()) {
        if (!currentSection.text.empty()) {
            currentSection.text += '\n';
        }
        currentSection.text += text;
    }
}

void SegmentPdfWalker::flushSection() {
    if (!currentSection.title.empty()) {
        result.emplace_back(currentSection);
        currentSection = {};
    }
}

bool SegmentPdfWalker::looksLikeTopLevelHeadingText(std::string_view text) {
    const auto trimmed = trim(text);
    if (trimmed.empty()) {
        return false;
    }

    return startsWithTopLevelNumberedHeading(trimmed) || startsWithAny(trimmed, {
               "ВВЕДЕНИЕ",
               "Введение",
               "ЗАКЛЮЧЕНИЕ",
               "Заключение",
               "СПИСОК",
               "Список",
               "ПРИЛОЖЕНИЕ",
               "Приложение",
           });
}

bool SegmentPdfWalker::startsWithTopLevelNumberedHeading(std::string_view text) {
    std::size_t i = 0;
    bool hasDigit = false;

    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        hasDigit = true;
        ++i;
    }

    if (!hasDigit) {
        return false;
    }

    if (i < text.size() && text[i] == '.') {
        return false;
    }

    return i < text.size() && (text[i] == ' ' || text[i] == '\t');
}

bool SegmentPdfWalker::startsWithAny(std::string_view text, std::initializer_list<std::string_view> prefixes) {
    for (const auto prefix : prefixes) {
        if (text.starts_with(prefix)) {
            return true;
        }
    }

    return false;
}

std::string_view SegmentPdfWalker::trim(std::string_view text) {
    while (!text.empty() && isAsciiSpace(text.front())) {
        text.remove_prefix(1);
    }

    while (!text.empty() && isAsciiSpace(text.back())) {
        text.remove_suffix(1);
    }

    return text;
}

bool SegmentPdfWalker::isAsciiSpace(char c) {
    return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\v';
}

bool SegmentPdfWalker::looksLikeManualTocRow(std::string_view text) {
    return hasDotLeaders(text) && endsWithPageNumber(text);
}

bool SegmentPdfWalker::hasDotLeaders(std::string_view text) {
    return text.find("...") != std::string_view::npos || text.find("…") != std::string_view::npos;
}

bool SegmentPdfWalker::endsWithPageNumber(std::string_view text) {
    text = trim(text);
    if (text.empty() || text.back() < '0' || text.back() > '9') {
        return false;
    }

    std::size_t i = text.size();
    while (i > 0 && text[i - 1] >= '0' && text[i - 1] <= '9') {
        --i;
    }

    return i > 0 && !isAsciiSpace(text[i - 1]);
}

std::string SegmentPdfWalker::normalizeTitle(std::string_view text) {
    std::string normalized;
    normalized.reserve(text.size());

    int32_t i = 0;
    const int32_t length = static_cast<int32_t>(text.length());
    const auto* data = reinterpret_cast<const uint8_t*>(text.data());
    bool previousSpace = true;

    while (i < length) {
        UChar32 ch = 0;
        U8_NEXT(data, i, length, ch);
        if (ch < 0 || u_isdigit(ch)) {
            continue;
        }

        if (ch == 0x00A0 || u_isspace(ch)) {
            if (!previousSpace) {
                normalized += ' ';
                previousSpace = true;
            }
            continue;
        }

        ch = u_tolower(ch);
        char buffer[4];
        int32_t offset = 0;
        auto* output = reinterpret_cast<uint8_t*>(buffer);
        U8_APPEND_UNSAFE(output, offset, ch);
        normalized.append(buffer, static_cast<std::size_t>(offset));
        previousSpace = false;
    }

    while (!normalized.empty() && normalized.back() == ' ') {
        normalized.pop_back();
    }

    return normalized;
}

bool SegmentPdfWalker::isCyrillic(UChar32 c) {
    UErrorCode status = U_ZERO_ERROR;
    const auto script = uscript_getScript(c, &status);
    return U_SUCCESS(status) && script == USCRIPT_CYRILLIC;
}

SegmentPdfWalker::TextCheckResult SegmentPdfWalker::checkText(std::string_view text) {
    TextCheckResult result;

    int32_t i = 0;
    const int32_t length = static_cast<int32_t>(text.length());
    const auto* data = reinterpret_cast<const uint8_t*>(text.data());
    while (i < length) {
        UChar32 ch = 0;
        U8_NEXT(data, i, length, ch);

        if (ch < 0) {
            result.isUpperCaseCompatible = false;
            return result;
        }

        if (u_isdigit(ch) || u_isspace(ch) || u_ispunct(ch)) {
            continue;
        }

        if (u_isalpha(ch)) {
            result.hasLetter = true;

            if (isCyrillic(ch)) {
                result.hasCyrillic = true;
            }

            if (!u_isupper(ch)) {
                result.isUpperCaseCompatible = false;
                return result;
            }
            continue;
        }
        result.isUpperCaseCompatible = false;
        return result;
    }
    return result;
}
}
