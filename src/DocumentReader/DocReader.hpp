#pragma once

#include "Paragraph.hpp"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace DocReader {
std::optional<std::string> xmlReader(std::string&& xml);

std::optional<std::vector<Documents::Paragraph>> zipReader(std::span<unsigned char> zip);

std::vector<std::string> splitText(const std::string& text);

std::vector<Documents::Paragraph> DocxReader(std::string_view xml);
} 