#pragma once

#include "Paragraph.hpp"

#include <pugixml.hpp>

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <unicode/uchar.h>

namespace Walker {
struct SegmentDocWalker : pugi::xml_tree_walker {
    SegmentDocWalker();
    explicit SegmentDocWalker(std::unordered_set<std::string> topLevelHeadingStyles,
                              std::vector<std::string> topLevelTocTitles = {},
                              bool allowTocTitleMatching = false);

    static std::unordered_set<std::string> defaultTopLevelHeadingStyles();

    bool for_each(pugi::xml_node& node) override;
    bool end(pugi::xml_node& node) override;

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

private:
    struct TextCheckResult {
        bool hasLetter = false;
        bool hasCyrillic = false;
        bool isUpperCaseCompatible = true;
    };

    void flushParagraph();
    void flushSection();
    bool isHeadingParagraph() const;
    bool isHeadingStyle(std::string_view style) const;
    bool matchesTopLevelTocTitle(std::string_view text) const;
    bool looksLikeManualTocRow() const;

    static bool looksLikeHeadingText(std::string_view text);
    static bool looksLikeTopLevelHeadingText(std::string_view text);
    static bool startsWithTopLevelNumberedHeading(std::string_view text);
    static bool startsWithAny(std::string_view text, std::initializer_list<std::string_view> prefixes);
    static std::string_view trim(std::string_view text);
    static bool isAsciiSpace(char c);
    static bool hasDotLeaderTab(pugi::xml_node paragraph);
    static bool hasDotLeaders(std::string_view text);
    static bool endsWithPageNumber(std::string_view text);
    static std::string normalizeHeadingForMatch(std::string_view text);
    static std::string stripLeadingTopLevelNumber(std::string text);
    static std::string normalizeTitle(std::string_view text);
    static bool isInsideTableOfContents(pugi::xml_node node);
    static bool hasCourierNewFont(pugi::xml_node node);
    static bool isCourierNew(std::string_view font);
    static bool isCyrillic(UChar32 c);
    static TextCheckResult checkText(std::string_view text);
};

struct SegmentPdfWalker {
    std::vector<Documents::Paragraph> result;
    Documents::Paragraph currentSection;

    void pushText(std::string_view text);
    void finish();

private:
    struct TextCheckResult {
        bool hasLetter = false;
        bool hasCyrillic = false;
        bool isUpperCaseCompatible = true;
    };

    void pushParagraph(std::string_view text);
    void flushSection();

    static bool looksLikeTopLevelHeadingText(std::string_view text);
    static bool startsWithKnownUnnumberedHeading(std::string_view text);
    static bool startsWithAppendixHeading(std::string_view text);
    static bool startsWithTopLevelNumberedHeading(std::string_view text);
    static bool startsWithAny(std::string_view text, std::initializer_list<std::string_view> prefixes);
    static std::string_view trim(std::string_view text);
    static bool isAsciiSpace(char c);
    static bool looksLikeManualTocRow(std::string_view text);
    static bool hasDotLeaders(std::string_view text);
    static bool endsWithPageNumber(std::string_view text);
    static std::string normalizeTitle(std::string_view text);
    static bool isCyrillic(UChar32 c);
    static TextCheckResult checkText(std::string_view text);
};
}
