#pragma once
#include <boost/json.hpp>

#include <string>

struct Document {
    std::string docId;
    std::string text;

    Document( std::string&& data,std::string docId): docId(std::move(docId)), text(std::move(data)) {}
};

namespace boost::json {
inline void tag_invoke(value_from_tag, value& jv, Document const& d) {
    object obj;

    obj["text"] = value_from(d.text);
    obj["id"] = d.docId;

    jv = std::move(obj);
}
}   // namespace boost::json