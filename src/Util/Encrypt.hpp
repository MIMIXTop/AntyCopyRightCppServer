#pragma once
#include <string>
#include <string_view>
#include <cstddef>
#include <vector>

namespace util {

std::string deriveUserKey(const std::string& userKey,const std::string serverKey);

std::string randomUrlSafeToken(std::size_t byteCount = 32);

std::string sha256Hex(std::string_view data);

std::string urlEncodeHelper(std::initializer_list<std::pair<std::string_view, std::string_view>> params);

}
