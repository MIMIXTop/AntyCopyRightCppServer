#include "ConfigParser.hpp"
#include "config.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <print>
#include <ranges>

namespace Util {
ConfigParser::ConfigParser() {
    const char* configPath = std::getenv("APP_CONFIG_FILE");
    if (configPath == nullptr || configPath[0] == '\0') {
        configPath = CONFIG_FILE_PATH;
    }

    std::fstream config_file { configPath };
    if (config_file.is_open()) {
        for (auto&& line : std::views::istream<std::string>(config_file)) {
            auto pos = line.find('=');
            if (pos != std::string_view::npos) {
                variables.emplace(line.substr(0, pos), line.substr(pos + 1));
            }
        }
    } else {
        std::println(std::cerr, "[ConfigParser] ВНИМАНИЕ: Не удалось открыть файл конфигурации: {}", configPath);
    }
}

std::string_view ConfigParser::operator[](const std::string& key) const {
    if (variables.contains(key))
        return variables.at(key);
    if (const char* value = std::getenv(key.c_str()); value != nullptr) {
        return value;
    }
    return {};
}
}   // namespace Util
