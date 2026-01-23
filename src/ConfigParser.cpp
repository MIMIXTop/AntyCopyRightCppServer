#include "ConfigParser.hpp"
#include "config.hpp"

#include <fstream>
#include <ranges>

namespace Util {
ConfigParser::ConfigParser() {
    std::fstream config_file { CONFIG_FILE_PATH };
    if (config_file.is_open()) {
        for (auto&& line : std::views::istream<std::string>(config_file)) {
            auto pos = line.find('=');
            if (pos != std::string_view::npos) {
                variables.emplace(line.substr(0, pos), line.substr(pos + 1));
            }
        }
    }
}

std::string_view ConfigParser::operator[](const std::string& key) const {
    if (variables.contains(key))
        return variables.at(key);
    return {};
}
}   // namespace Util