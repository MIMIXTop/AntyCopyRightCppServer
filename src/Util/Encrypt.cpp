//
// Created by mimixtop on 29.03.2026.
//

#include "Encrypt.hpp"

#include <span>
#include <stdexcept>
#include <vector>
#include <cstddef>
#include <boost/url/authority_view.hpp>
#include <boost/url/url.hpp>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/rand.h>

namespace {

std::string base64UrlEncode(std::span<const unsigned char> data) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

    std::string result;
    result.reserve((data.size() * 4 + 2) / 3);

    int val = 0;
    int valb = -6;

    for (auto&& c : data) {
        val = (val << 8) + c;
        valb += 8;

        while (valb >= 0) {
            result.push_back(alphabet[(val >> valb) & 0x3f]);
            valb -= 6;
        }
    }

    if (valb > -6) {
        result.push_back(alphabet[(val << 8) >> (valb + 8) & 0x3f]);
    }

    return result;
}

std::string byteToHex(std::span<const unsigned char> bytes) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);

    for (auto&& byte : bytes) {
        result.push_back(hex[(byte >> 4) & 0x0f]);
        result.push_back(hex[(byte & 0x0f)]);
    }

    return result;
}

}

namespace util {

std::string deriveUserKey(const std::string& userKey,const std::string serverKey) {
    unsigned char hash[SHA256_DIGEST_LENGTH];

    SHA256_CTX sha256;
    SHA256_Init(&sha256);

    SHA256_Update(&sha256, serverKey.c_str(), serverKey.length());
    SHA256_Update(&sha256, userKey.c_str(), userKey.length());

    SHA256_Final(hash, &sha256);

    return std::string(reinterpret_cast<const char*>(hash), SHA256_DIGEST_LENGTH);
}
std::string randomUrlSafeToken(std::size_t byteCount) {
    std::vector<unsigned char> data(byteCount);

    if (RAND_bytes(data.data(), data.size()) != 1) {
        throw std::runtime_error("Random Bytes Generator Error");
    }

    return  base64UrlEncode(data);
}

std::string sha256Hex(std::string_view data) {
    unsigned char hash[SHA256_DIGEST_LENGTH];

    SHA256(
    reinterpret_cast<const unsigned char*>(data.data()),
    data.size(),
    hash
    );

    return byteToHex(std::span(hash, SHA256_DIGEST_LENGTH));
}
std::string urlEncodeHelper(std::initializer_list<std::pair<std::string_view, std::string_view>> params) {

    boost::urls::url u;

    for (auto&& [key, value] : params) {
        u.params().append({key, value});
    }

    return u.query();

}


} // util