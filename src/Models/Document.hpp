#pragma once
#include "Paragraph.hpp"

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

inline Document tag_invoke(value_to_tag<Document>, value const& jv) {
    auto const& obj = jv.as_object();

    std::vector<Documents::Paragraph> text;

    if (auto const* sections = obj.if_contains("document_sections")) {
        auto const& sections_array = sections->as_array();
        text.reserve(sections_array.size());

        for (auto const& section_value : sections_array) {
            auto const& section = section_value.as_object();

            text.emplace_back(
                std::string(section.at("title").as_string()),
                std::string(section.at("content").as_string())
            );
        }
    } else if (auto const* text_value = obj.if_contains("text")) {
        auto const& text_array = text_value->as_array();
        text.reserve(text_array.size());

        for (auto const& paragraph_value : text_array) {
            auto const& paragraph = paragraph_value.as_object();

            text.emplace_back(
                std::string(paragraph.at("title").as_string()),
                std::string(paragraph.at("text").as_string())
            );
        }
    }

    auto const* external_id = obj.if_contains("external_id");
    auto const& id = external_id != nullptr ? *external_id : obj.at("id");

    return Document(std::move(text), std::string(id.as_string()));
}
}   // namespace boost::json
