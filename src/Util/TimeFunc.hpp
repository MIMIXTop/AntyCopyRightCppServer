#pragma once
#include <chrono>
#include <string>
#include <format>

namespace util::time {
inline std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_sec = std::chrono::floor<std::chrono::seconds>(now);
    return std::format("{:%FT%TZ}", now_sec);
}

inline std::string getCurrentTimeAfterMinutes(int minutes) {
    auto now = std::chrono::system_clock::now() + std::chrono::minutes{minutes};
    auto now_sec = std::chrono::floor<std::chrono::seconds>(now);
    return std::format("{:%FT%TZ}", now_sec);
}
}