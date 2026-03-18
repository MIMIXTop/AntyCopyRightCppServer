#pragma once

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace DocReader {
std::optional<std::string> xmlReader(std::string&& xml);

std::optional<std::string> zipReader(std::span<unsigned char> zip);

std::vector<std::string> splitText(const std::string& text);
} 