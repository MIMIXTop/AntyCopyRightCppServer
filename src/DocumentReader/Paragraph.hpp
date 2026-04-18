#pragma once
#include <string>
#include <boost/json.hpp>

namespace Documents {

struct Paragraph {
    std::string title;
    std::string text;
};

inline void tag_invoke(boost::json::value_from_tag, boost::json::value& jv, Paragraph const& par) {
    jv = {
        {"title", par.title},
        {"text", par.text},
    };
}
}