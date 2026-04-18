#pragma once

#include "Paragraph.hpp"

#include <pugixml.hpp>

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>
#include <unicode/uchar.h>
#include <unicode/utf8.h>
#include <unicode/uscript.h>

namespace Walker {
struct SegmentDocWalker : pugi::xml_tree_walker {
    SegmentDocWalker()
      : SegmentDocWalker(defaultTopLevelHeadingStyles(), {}) {}

    explicit SegmentDocWalker(std::unordered_set<std::string> topLevelHeadingStyles,
                              std::vector<std::string> topLevelTocTitles = {},
                              bool allowTocTitleMatching = false)
      : topLevelHeadingStyles(std::move(topLevelHeadingStyles)), allowTocTitleMatching(allowTocTitleMatching) {
        normalizedTopLevelTocTitles.reserve(topLevelTocTitles.size());
        for (const auto& title : topLevelTocTitles) {
            auto normalizedTitle = normalizeHeadingForMatch(title);
            if (!normalizedTitle.empty()) {
                normalizedTopLevelTocTitles.emplace_back(std::move(normalizedTitle));
            }
        }
    }

    static std::unordered_set<std::string> defaultTopLevelHeadingStyles() {
        return { "Heading1", "1654", "1702" };
    }

    std::vector<Documents::Paragraph> result;
    Documents::Paragraph currentSection;
    std::string paragraphText;
    std::string paragraphStyle;
    std::unordered_set<std::string> topLevelHeadingStyles;
    std::vector<std::string> normalizedTopLevelTocTitles;
    bool allowTocTitleMatching = false;
    bool paragraphHasLetter = false;
    bool paragraphIsUpperCase = true;
    bool paragraphHasCyrillic = false;
    bool paragraphHasTocBookmark = false;
    bool paragraphHasDotLeaderTab = false;
    bool skipCurrentRunText = false;

    bool for_each(pugi::xml_node& node) override {
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

    bool end(pugi::xml_node& node) override {
        flushParagraph();
        flushSection();
        return true;
    }

private:
    void flushParagraph() {
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

    void flushSection() {
        if (!currentSection.title.empty()) {
            result.emplace_back(currentSection);
            currentSection = {};
        }
    }

    struct TextCheckResult {
        bool hasLetter = false;
        bool hasCyrillic = false;
        bool isUpperCaseCompatible = true;
    };

    bool isHeadingParagraph() const {
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

    bool isHeadingStyle(std::string_view style) const {
        return !style.empty() && topLevelHeadingStyles.contains(std::string(style));
    }

    static bool looksLikeHeadingText(std::string_view text) {
        return looksLikeTopLevelHeadingText(text);
    }

    static bool looksLikeTopLevelHeadingText(std::string_view text) {
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

    static bool startsWithTopLevelNumberedHeading(std::string_view text) {
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

    static bool startsWithAny(std::string_view text, std::initializer_list<std::string_view> prefixes) {
        for (const auto prefix : prefixes) {
            if (text.starts_with(prefix)) {
                return true;
            }
        }

        return false;
    }

    static std::string_view trim(std::string_view text) {
        while (!text.empty() && isAsciiSpace(text.front())) {
            text.remove_prefix(1);
        }

        while (!text.empty() && isAsciiSpace(text.back())) {
            text.remove_suffix(1);
        }

        return text;
    }

    static bool isAsciiSpace(char c) {
        return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\v';
    }

    bool matchesTopLevelTocTitle(std::string_view text) const {
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

    bool looksLikeManualTocRow() const {
        return paragraphHasDotLeaderTab || (hasDotLeaders(paragraphText) && endsWithPageNumber(paragraphText));
    }

    static bool hasDotLeaderTab(pugi::xml_node paragraph) {
        const auto tabs = paragraph.child("w:pPr").child("w:tabs");
        for (const auto tab : tabs.children("w:tab")) {
            if (std::string_view(tab.attribute("w:leader").value()) == "dot") {
                return true;
            }
        }

        return false;
    }

    static bool hasDotLeaders(std::string_view text) {
        return text.find("...") != std::string_view::npos || text.find("…") != std::string_view::npos;
    }

    static bool endsWithPageNumber(std::string_view text) {
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

    static std::string normalizeHeadingForMatch(std::string_view text) {
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

    static std::string stripLeadingTopLevelNumber(std::string text) {
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

    static std::string normalizeTitle(std::string_view text) {
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

    static bool isInsideTableOfContents(pugi::xml_node node) {
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

    static bool hasCourierNewFont(pugi::xml_node node) {
        return isCourierNew(node.attribute("w:cs").value()) || isCourierNew(node.attribute("w:ascii").value()) ||
               isCourierNew(node.attribute("w:hAnsi").value());
    }

    static bool isCourierNew(std::string_view font) { return font == "Courier New"; }

    static bool isCyrillic(UChar32 c) {
        UErrorCode status = U_ZERO_ERROR;
        const auto script = uscript_getScript(c, &status);
        return U_SUCCESS(status) && script == USCRIPT_CYRILLIC;
    }

    static TextCheckResult checkText(std::string_view text) {
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
};

struct DocWalker : pugi::xml_tree_walker {
    std::string result;
    bool skipNextText = false;

    bool for_each(pugi::xml_node& node) override {
        std::string name = node.name();
        if (name == "w:rFonts" && std::string(node.attribute("w:cs").value()) == "Courier New") {
            skipNextText = true;
        }
        if (name == "w:t") {
            if (skipNextText) {
                skipNextText = false;
                return true;
            }
            result += node.text().as_string();
        }

        return true;
    }
};
}
