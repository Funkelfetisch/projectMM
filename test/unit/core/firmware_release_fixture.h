// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// Signed releases for the host tests of every firmware update path: a disposable key, the substitute signature check that accepts it, and images with their claims.

#include "core/util/sha256.h"
#include "doctest.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {
constexpr uint32_t kOtaSecurityVersion = 3;
constexpr uint32_t kOtaEsp32Target = 0x2aa0a5f1u;   // FNV-1a of "nebulite-esp32", the value firmware_signing.py computes for the same name

// A disposable public key: the host links no crypto library, so these tests pin what the service hands the primitive, and check_firmware_signature.py pins the P-256 arithmetic.
const std::array<uint8_t, 65> kOtaKey = [] {
    std::array<uint8_t, 65> key{};
    key[0] = 0x04;
    for (size_t i = 1; i < key.size(); ++i) key[i] = static_cast<uint8_t>(i * 7 + 1);
    return key;
}();

// The substitute primitive accepts exactly the signature a disposable scheme derives from the key and the signed digest.
std::array<uint8_t, 64> otaSignatureOf(const uint8_t* signedDigest) {
    std::array<uint8_t, 64> signature{};
    mm::Sha256 first, second;
    first.update(kOtaKey.data(), kOtaKey.size()); first.update(signedDigest, 32); first.digest(signature.data());
    second.update(signedDigest, 32); second.update(kOtaKey.data(), kOtaKey.size()); second.digest(signature.data() + 32);
    return signature;
}
bool otaSignatureCheck(const uint8_t* publicKey, size_t publicKeySize, const uint8_t* digest, size_t digestSize,
                       const uint8_t* signature, size_t signatureSize) {
    if (publicKeySize != kOtaKey.size() || std::memcmp(publicKey, kOtaKey.data(), kOtaKey.size()) != 0 ||
        digestSize != 32 || signatureSize != 64) return false;
    const auto expected = otaSignatureOf(digest);
    return std::memcmp(expected.data(), signature, 64) == 0;
}

std::string hexOf(const uint8_t* bytes, size_t count) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string text;
    for (size_t i = 0; i < count; ++i) { text.push_back(digits[bytes[i] >> 4]); text.push_back(digits[bytes[i] & 15]); }
    return text;
}
std::vector<uint8_t> otaImage(size_t size, uint8_t seed = 1) {
    std::vector<uint8_t> image(size);
    for (size_t i = 0; i < size; ++i) image[i] = static_cast<uint8_t>(i * 31 + seed);
    return image;
}

// What the publisher hands the phone: the 60 signed claims and the signature over their SHA-256, both as hex.
struct OtaRelease {
    std::vector<uint8_t> image;
    std::string claims, signature;
};
OtaRelease otaRelease(std::vector<uint8_t> image, uint32_t target = kOtaEsp32Target, uint32_t version = kOtaSecurityVersion) {
    std::array<uint8_t, 60> claims{};
    std::memcpy(claims.data(), "HELIO-FW-AUTH-V1", 16);
    const auto put = [&](size_t at, uint32_t value) { for (int i = 0; i < 4; ++i) claims[at + i] = static_cast<uint8_t>(value >> (24 - 8 * i)); };
    put(16, target); put(20, static_cast<uint32_t>(image.size())); put(24, version);
    mm::Sha256 imageHash; imageHash.update(image.data(), image.size()); imageHash.digest(claims.data() + 28);
    uint8_t signedDigest[32];
    mm::Sha256 claimsHash; claimsHash.update(claims.data(), claims.size()); claimsHash.digest(signedDigest);
    const auto signature = otaSignatureOf(signedDigest);
    return {std::move(image), hexOf(claims.data(), claims.size()), hexOf(signature.data(), signature.size())};
}

}  // namespace
