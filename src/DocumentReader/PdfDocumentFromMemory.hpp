#pragma once

#include <poppler/cpp/poppler-document.h>
#include <cstdint>
#include <memory>
#include <span>

class PdfDocumentFromMemory {
public:
    PdfDocumentFromMemory(std::span<unsigned char> data);
    poppler::document& document();
    const poppler::document& document() const;

private:
    std::span<unsigned char> data_;
    std::unique_ptr<poppler::document> document_;
};