#pragma once
#include <boost/json.hpp>

#include <string>

struct Document {
    std::string docId;
    std::vector<Documents::Paragraph> text;

    Document(std::vector<Documents::Paragraph>&& data,std::string docId): docId(std::move(docId)), text(std::move(data)) {}
};

namespace boost::json {
inline void tag_invoke(value_from_tag, value& jv, Document const& d) {
    jv = {
        {"id", d.docId},
        {"text", d.text},
    };
}
}   // namespace boost::json