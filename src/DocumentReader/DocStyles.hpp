#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace DocReader {

struct StyleInfo {
    std::string id;
    std::string name;
    std::string basedOn;
    std::optional<int> outlineLevel;
};

struct DocumentStyles {
    bool hasStyleDefinitions = false;
    std::unordered_map<std::string, StyleInfo> styles;
    std::unordered_set<std::string> topLevelHeadingStyles;
};

DocumentStyles parseStyles(std::string_view stylesXml);

}  // namespace DocReader
