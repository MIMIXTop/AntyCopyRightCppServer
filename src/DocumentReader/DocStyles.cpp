#include "DocStyles.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <string>
#include <unordered_set>

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

std::optional<int> resolveOutlineLevel(const std::unordered_map<std::string, DocReader::StyleInfo>& styles,
                                       std::string_view styleId,
                                       std::unordered_set<std::string>& visiting) {
    const auto styleIt = styles.find(std::string(styleId));
    if (styleIt == styles.end()) {
        return std::nullopt;
    }

    const auto& style = styleIt->second;
    if (style.outlineLevel.has_value()) {
        return style.outlineLevel;
    }

    if (style.basedOn.empty() || visiting.contains(style.id)) {
        return std::nullopt;
    }

    visiting.insert(style.id);
    const auto inheritedOutlineLevel = resolveOutlineLevel(styles, style.basedOn, visiting);
    visiting.erase(style.id);
    return inheritedOutlineLevel;
}

bool isHeadingOneFallback(const DocReader::StyleInfo& style) {
    if (style.id == "Heading1") {
        return true;
    }

    return asciiLower(style.name) == "heading 1";
}
}  // namespace

namespace DocReader {

DocumentStyles parseStyles(std::string_view stylesXml) {
    DocumentStyles documentStyles;
    if (stylesXml.empty()) {
        return documentStyles;
    }

    pugi::xml_document doc;
    const std::string xml(stylesXml);
    const auto parseResult = doc.load_string(xml.c_str(), pugi::parse_default | pugi::parse_ws_pcdata);
    if (!parseResult) {
        return documentStyles;
    }

    const auto stylesRoot = doc.child("w:styles");
    if (!stylesRoot) {
        return documentStyles;
    }

    documentStyles.hasStyleDefinitions = true;

    for (const auto styleNode : stylesRoot.children("w:style")) {
        StyleInfo style;
        style.id = styleNode.attribute("w:styleId").value();
        if (style.id.empty()) {
            continue;
        }

        style.name = styleNode.child("w:name").attribute("w:val").value();
        style.basedOn = styleNode.child("w:basedOn").attribute("w:val").value();

        const std::string_view outlineValue = styleNode.child("w:pPr").child("w:outlineLvl").attribute("w:val").value();
        if (!outlineValue.empty()) {
            style.outlineLevel = parseInt(outlineValue);
        }

        documentStyles.styles.emplace(style.id, std::move(style));
    }

    for (const auto& [styleId, style] : documentStyles.styles) {
        std::unordered_set<std::string> visiting;
        const auto outlineLevel = resolveOutlineLevel(documentStyles.styles, styleId, visiting);
        if (outlineLevel.has_value()) {
            if (*outlineLevel == 0) {
                documentStyles.topLevelHeadingStyles.insert(styleId);
            }
            continue;
        }

        if (isHeadingOneFallback(style)) {
            documentStyles.topLevelHeadingStyles.insert(styleId);
        }
    }

    return documentStyles;
}

}  // namespace DocReader
