#pragma once
#include "DocumentReader/Paragraph.hpp"

#include <boost/json.hpp>

#include <string>
#include <vector>

struct Document {
    std::string docId;
    std::vector<Documents::Paragraph> text;

    Document(std::vector<Documents::Paragraph>&& data, std::string docId) : docId(std::move(docId)), text(std::move(data)) {}
};

namespace boost::json {
inline void tag_invoke(value_from_tag, value& jv, Document const& d) {
    array text;
    text.reserve(d.text.size());

    for (const auto& paragraph : d.text) {
        text.emplace_back(value_from(paragraph));
    }

    jv = {
        {"id", d.docId},
        {"text", std::move(text)},
    };
}
}   // namespace boost::json
