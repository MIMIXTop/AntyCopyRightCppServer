#pragma once
#include "Paragraph.hpp"

#include <boost/json.hpp>

#include <string>
#include <vector>

struct Document {
    std::string docId;
    std::string title;
    std::vector<Documents::Paragraph> text;

    Document() = default;
    Document(std::vector<Documents::Paragraph>&& data, std::string docId, std::string title = {}) : docId(std::move(docId)), text(std::move(data)), title(std::move(title)) {}
};

namespace boost::json {
inline void tag_invoke(value_from_tag, value& jv, Document const& d) {
    array text_arr;
    text_arr.reserve(d.text.size());

    for (const auto& paragraph : d.text) {
        text_arr.emplace_back(value_from(paragraph));
    }

    jv = {
        {"id", d.docId},
        {"external_id", d.docId},
        {"title", d.title},
        {"text", std::move(text_arr)},
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

            std::string p_title;
            if (auto const* t = section.if_contains("title")) p_title = std::string(t->as_string());

            std::string p_content;
            if (auto const* c = section.if_contains("content")) {
                p_content = std::string(c->as_string());
            } else if (auto const* t = section.if_contains("text")) {
                p_content = std::string(t->as_string());
            }

            text.emplace_back(std::move(p_title), std::move(p_content));
        }
    } else if (auto const* text_value = obj.if_contains("text")) {
        auto const& text_array = text_value->as_array();
        text.reserve(text_array.size());

        for (auto const& paragraph_value : text_array) {
            auto const& paragraph = paragraph_value.as_object();

            std::string p_title;
            if (auto const* t = paragraph.if_contains("title")) p_title = std::string(t->as_string());

            std::string p_content;
            if (auto const* t = paragraph.if_contains("text")) {
                p_content = std::string(t->as_string());
            } else if (auto const* c = paragraph.if_contains("content")) {
                p_content = std::string(c->as_string());
            }

            text.emplace_back(std::move(p_title), std::move(p_content));
        }
    }

    std::string id_str;
    if (auto const* external_id = obj.if_contains("external_id")) {
        id_str = std::string(external_id->as_string());
    } else if (auto const* id = obj.if_contains("id")) {
        id_str = std::string(id->as_string());
    }

    std::string title_str;
    if (auto const* title_val = obj.if_contains("title")) {
        title_str = std::string(title_val->as_string());
    } else if (!text.empty()) {
        title_str = text.front().title;
    }

    return Document(std::move(text), std::move(id_str), std::move(title_str));
}
} // namespace boost::json
