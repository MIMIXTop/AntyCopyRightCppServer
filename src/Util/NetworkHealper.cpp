#include "NetworkHealper.hpp"
#include <boost/algorithm/string.hpp>
#include <ranges>
#include <vector>

namespace util::network {
std::map<std::string, std::string> parse_cookie(std::string_view cookie_header) {
    std::map<std::string, std::string> cookie;

    for (auto&& chunk : cookie_header | std::views::split(';')) {
        std::string pair(std::ranges::data(chunk), std::ranges::size(chunk));

        boost::trim(pair);

        if (pair.empty()) continue;

        if (auto pos = pair.find('='); pos != std::string_view::npos) {
            cookie.emplace(
              pair.substr(0, pos),
              pair.substr(pos + 1)
            );
        }
    }

    return cookie;
}

bool verifPath(boost::url_view target) {
    auto segments = target.encoded_segments();

    std::vector<std::string_view> parse;
    for (auto segment : segments) {
        parse.push_back(segment);
    }

    if (parse.size() == 5 &&
        parse[2] == "courses" &&
        parse[4] == "courseWork") {
        return true;
    }

    if (parse.size() == 3 &&
        parse[2] == "courses") {
        return true;
    }

    if (parse.size() == 5 &&
        parse[2] == "courses" &&
        parse[4] == "students") {
        return true;
    }

    if (parse.size() == 7 &&
        parse[2] == "courses" &&
        parse[4] == "courseWork" &&
        parse[6] == "studentSubmissions") {
        return true;
    }

    return false;
}
}
