#include "DocReader.hpp"

#include "Walker.hpp"

#include <algorithm>
#include <mz.h>
#include <mz_strm.h>
#include <mz_strm_mem.h>
#include <mz_zip.h>
#include <span>
#include <string>
#include <vector>
#include <sys/stat.h>

std::optional<std::string> DocReader::xmlReader(std::string&& xml) {
    pugi::xml_document doc;
    doc.load_string(xml.c_str(), pugi::parse_default | pugi::parse_ws_pcdata);
    Walker::DocWalker walker;
    doc.traverse(walker);
    return walker.result;
}

std::optional<std::vector<Documents::Paragraph>> DocReader::zipReader(std::span<unsigned char> zip) {
    void* mem_stream = NULL;
    void* zip_handle = NULL;

    mem_stream = mz_stream_mem_create();
    mz_stream_mem_set_buffer(mem_stream, zip.data(), zip.size());

    if (mz_stream_open(mem_stream, NULL, MZ_OPEN_MODE_READ) != MZ_OK) {
        mz_stream_mem_delete(&mem_stream);
        throw std::runtime_error("Failed to set memory buffer");
    }

    zip_handle = mz_zip_create();
    if (!zip_handle) {
        mz_stream_close(mem_stream);
        mz_stream_mem_delete(&mem_stream);
        throw std::runtime_error("Failed to create ZIP handle");
    }

    if (mz_zip_open(zip_handle, mem_stream, MZ_OPEN_MODE_READ) != MZ_OK) {
        mz_zip_delete(&zip_handle);
        mz_stream_close(mem_stream);
        mz_stream_mem_delete(&mem_stream);
        throw std::runtime_error("Failed to open zip");
    }
    if (mz_zip_locate_entry(zip_handle, "word/document.xml", 0) != MZ_OK) {
        mz_zip_close(zip_handle);
        mz_zip_delete(&zip_handle);
        mz_stream_close(mem_stream);
        mz_stream_mem_delete(&mem_stream);
        return std::nullopt;
    }

    if (mz_zip_entry_read_open(zip_handle, 0, nullptr) != MZ_OK) {
        mz_zip_close(zip_handle);
        mz_zip_delete(&zip_handle);
        mz_stream_close(mem_stream);
        mz_stream_mem_delete(&mem_stream);
        throw std::runtime_error("Failed to open entry for reading");
    }
    std::vector<uint8_t> output;
    output.resize(65536);
    int64_t total = 0;
    int32_t read;
    while ((read = mz_zip_entry_read(zip_handle, output.data() + total, output.size() - total)) > 0) {
        total += read;
        if (total >= output.size()) {
            output.resize(output.size() * 2);
        }
    }
    output.resize(total);

    mz_zip_entry_close(zip_handle);
    mz_zip_close(zip_handle);
    mz_zip_delete(&zip_handle);
    mz_stream_close(mem_stream);
    mz_stream_mem_delete(&mem_stream);

    return DocxReader(std::string_view(std::string(output.begin(), output.end())));
}

std::vector<std::string> DocReader::splitText(const std::string& text) { return std::vector<std::string>(); }

std::vector<Documents::Paragraph> DocReader::DocxReader(const std::string_view xml) {
    pugi::xml_document doc;
    doc.load_string(xml.data(), pugi::parse_default | pugi::parse_ws_pcdata);
    Walker::SegmentDocWalker walker;
    doc.traverse(walker);
    return walker.result;
}