#include "TimeFunc.hpp"

#include <chrono>
#include <format>
#include <optional>
#include <spanstream>

namespace {

std::optional<std::chrono::sys_seconds> parse_utc_time(std::string_view timestamp) {
    std::chrono::sys_seconds tp;

    for (const char* format : {"%FT%TZ", "%FT%T%Ez", "%FT%T%z", "%F %T%Ez", "%F %T%z"}) {
        std::ispanstream stream { std::span { timestamp } };
        if (stream >> std::chrono::parse(format, tp)) {
            return tp;
        }
    }

    return std::nullopt;
}

}   // namespace

namespace util::time {

std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto now_sec = std::chrono::floor<std::chrono::seconds>(now);
    return std::format("{:%FT%TZ}", now_sec);
}

std::string getCurrentTimeAfterMinutes(int minutes) {
    auto now = std::chrono::system_clock::now() + std::chrono::minutes { minutes };
    auto now_sec = std::chrono::floor<std::chrono::seconds>(now);
    return std::format("{:%FT%TZ}", now_sec);
}

std::string getCurrentTimeAfterSeconds(int seconds) {
    auto now = std::chrono::system_clock::now() + std::chrono::seconds { seconds };
    auto now_sec = std::chrono::floor<std::chrono::seconds>(now);
    return std::format("{:%FT%TZ}", now_sec);
}

bool isTimestampAfterNowPlus(std::string_view timestamp, int seconds) {
    return parse_utc_time(timestamp)
        .transform([seconds](std::chrono::sys_seconds parsed_time) {
            auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
            auto deadline = now + std::chrono::seconds { seconds };
            return parsed_time > deadline;
        })
        .value_or(false);
}

}   // namespace util::time
