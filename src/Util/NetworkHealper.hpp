#pragma once

#include <map>
#include <string>
#include <boost/url/urls.hpp>

namespace util::network {
std::map<std::string, std::string> parse_cookie(std::string_view cookie_header);

bool verifPath(boost::url_view target);
}
