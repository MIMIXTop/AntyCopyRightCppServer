#pragma once
#include <string>
#include <string_view>

namespace util::time {
std::string getCurrentTimestamp();

std::string getCurrentTimeAfterMinutes(int minutes);

std::string getCurrentTimeAfterSeconds(int seconds);

bool isTimestampAfterNowPlus(std::string_view timestamp, int seconds);
}