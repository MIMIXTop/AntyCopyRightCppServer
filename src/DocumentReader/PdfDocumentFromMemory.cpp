//
// Created by mimixtop on 29.04.2026.
//

#include "PdfDocumentFromMemory.hpp"

#include <limits>
PdfDocumentFromMemory::PdfDocumentFromMemory(std::span<unsigned char> data) : data_(std::move(data)) {
    if (data_.empty()) {
        throw std::invalid_argument("PdfDocumentFromMemory: data is empty");
    }

    if (data_.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::out_of_range("PdfDocumentFromMemory: data is too large");
    }

    document_.reset(poppler::document::load_from_raw_data(
        reinterpret_cast<const char*>(data_.data()), data_.size()
    ));
}

poppler::document& PdfDocumentFromMemory::document() {
    return *document_;
}
const poppler::document& PdfDocumentFromMemory::document() const {
    return *document_;
}