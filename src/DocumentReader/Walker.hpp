#pragma once

#include "Paragraph.hpp"

#include <pugixml.hpp>

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>
#include <unicode/uchar.h>
#include <unicode/utf8.h>
#include <unicode/uscript.h>

namespace Walker {
struct SegmentDocWalker : pugi::xml_tree_walker {
    std::vector<Documents::Paragraph> result;
    Documents::Paragraph currentSection;
    std::string paragraphText;
    std::string paragraphStyle;
    bool paragraphHasLetter = false;
    bool paragraphIsUpperCase = true;
    bool paragraphHasCyrillic = false;
    bool paragraphHasTocBookmark = false;
    bool skipCurrentRunText = false;

    bool for_each(pugi::xml_node& node) override {
        std::string_view name = node.name();

        if (name == "w:p") {
            flushParagraph();
            paragraphStyle = node.child("w:pPr").child("w:pStyle").attribute("w:val").value();
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
            const bool isHeading = paragraphHasLetter && paragraphHasCyrillic && isHeadingParagraph();
            if (isHeading) {
                flushSection();
                currentSection.title = paragraphText;
            } else if (!currentSection.title.empty()) {
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

        if (paragraphHasTocBookmark && looksLikeHeadingText(paragraphText)) {
            return true;
        }

        return paragraphIsUpperCase && looksLikeTopLevelHeadingText(paragraphText);
    }

    static bool isHeadingStyle(std::string_view style) {
        if (style == "Heading1") {
            return true;
        }

        return style == "1654" || style == "1702";
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
