//
// Created by mimixtop on 29.03.2026.
//

#include "Encrypt.hpp"
#include <openssl/evp.h>
#include <openssl/sha.h>

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

} // util