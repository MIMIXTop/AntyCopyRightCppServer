//
// Created by mimixtop on 29.03.2026.
//

#include "Encrypt.hpp"

#include <span>
#include <stdexcept>
#include <vector>
#include <algorithm>
#include <ranges>
#include <cstddef>
#include <boost/url/authority_view.hpp>
#include <boost/url/url.hpp>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/rand.h>

#include <cryptopp/aes.h>
#include <cryptopp/gcm.h>
#include <cryptopp/filters.h>
#include <cryptopp/osrng.h>
#include <cryptopp/base64.h>


namespace {

constexpr int AEG_KEY_LEN = 32;
constexpr int IV_LEN = 12;
constexpr int TAG_LEN = 16;

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
std::string textEncrypt(std::string_view text, std::string_view key) {
    if (key.empty()) {
        throw std::runtime_error("Invalid Key Length");
    }

    CryptoPP::AutoSeededRandomPool prng;

    CryptoPP::byte iv[IV_LEN];
    prng.GenerateBlock(iv, sizeof(iv));

    std::string ciphertext;

    try {
        unsigned char keyBuffer[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(key.data()), key.size(), keyBuffer);

        CryptoPP::GCM<CryptoPP::AES>::Encryption e;
        e.SetKeyWithIV(reinterpret_cast<const CryptoPP::byte*>(keyBuffer), AEG_KEY_LEN, iv, sizeof(iv));

        std::string input(text);
        CryptoPP::StringSource ss1(
            reinterpret_cast<const CryptoPP::byte*>(input.data()),
            input.size(),
            true,
            new CryptoPP::AuthenticatedEncryptionFilter(
                e, new CryptoPP::StringSink(ciphertext), false, TAG_LEN
            )
        );

        std::string combined = std::string(reinterpret_cast<char*>(iv), IV_LEN) + ciphertext;

        std::string encoded;
        CryptoPP::StringSource ss2(
            reinterpret_cast<const CryptoPP::byte*>(combined.data()),
            combined.size(),
            true,
            new CryptoPP::Base64Encoder(
                new CryptoPP::StringSink(encoded),
                false
            )
        );

        return encoded;

    } catch (const CryptoPP::Exception& ex) {
        throw std::runtime_error(std::string("Ошибка шифрования: ") + ex.what());
    }
}

std::string textDecrypt(std::string_view text, std::string_view key) {
    if (key.empty()) {
        throw std::runtime_error("Invalid Key Length");
    }

    try {
        std::string input(text);
        std::string combined_binary;
        CryptoPP::StringSource ss_base64(
            reinterpret_cast<const CryptoPP::byte*>(input.data()),
            input.size(),
            true,
            new CryptoPP::Base64Decoder(
                new CryptoPP::StringSink(combined_binary)
            )
        );

        if (combined_binary.size() < IV_LEN + TAG_LEN) {
            throw std::runtime_error("Неверный формат или длина зашифрованных данных");
        }

        CryptoPP::byte iv[IV_LEN];
        std::ranges::copy(combined_binary | std::views::take(IV_LEN), iv);
        auto ciphertext_view = combined_binary | std::views::drop(IV_LEN);
        std::string ciphertext(ciphertext_view.begin(), ciphertext_view.end());
        std::string decoded;

        CryptoPP::GCM<CryptoPP::AES>::Decryption dec;
        unsigned char keyBuffer[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(key.data()), key.size(), keyBuffer);
        dec.SetKeyWithIV(reinterpret_cast<const CryptoPP::byte*>(keyBuffer), AEG_KEY_LEN, iv, sizeof(iv));

        CryptoPP::AuthenticatedDecryptionFilter df(
            dec,
            new CryptoPP::StringSink(decoded),
            CryptoPP::AuthenticatedDecryptionFilter::DEFAULT_FLAGS,
            TAG_LEN
        );

        CryptoPP::StringSource ss_decoded(
            reinterpret_cast<const CryptoPP::byte*>(ciphertext.data()),
            ciphertext.size(),
            true,
            new CryptoPP::Redirector(df)
        );

        if (!df.GetLastResult()) {
            throw std::runtime_error("Данные были изменены или использован неверный ключ (провал проверки MAC)");
        }

        return decoded;
    }  catch (const CryptoPP::Exception& ex) {
        throw std::runtime_error(std::string("Ошибка расшифровки (возможно, неверный ключ или данные повреждены): ") + ex.what());
    }
}

} // util
