// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

/// SHA-256 as FIPS 180-4 defines it, carried in the tree rather than linked from a library.
///
/// @technical
///
/// ## Why a copy rather than a library
///
/// This is the only cryptographic primitive MoonLight needs, and it needs it on all five targets.
/// OpenSSL does not exist on ESP32, where ESP-IDF ships mbedtls, so linking it would mean an mbedtls path for devices and an OpenSSL path for the desktop.
/// That is two implementations of one function that must agree byte for byte, and a divergence produces identifiers that silently differ between platforms.
/// One vendored file is the smaller thing to own, and the algorithm is frozen.
/// FIPS 180-4 has not changed since 2015 and its test vectors are published, so `unit_sha256.cpp` pins this against them.
///
/// ## What it is not
///
/// This is no general-purpose crypto layer: no HMAC, authentication or constant-time comparison.
///
/// ## Incremental hashing and truncation
///
/// Firmware images may be larger than available RAM; the incremental form keeps one partial block.
///
/// `sha256Hex` truncates, which is the standard way to derive a shorter identifier (NIST SP 800-107 section 5.1).
/// At the default of 16 bytes it leaves 128 bits, far past any collision concern for a population of LED controllers.

#include <cstddef>
#include <cstdint>

namespace mm {

/// Digest length in bytes.
inline constexpr size_t kSha256DigestSize = 32;

/// Incremental SHA-256; digest does not consume the running state.
class Sha256 {
public:
    /// Reject a null nonempty chunk or a message too large for the FIPS bit-length field.
    bool update(const void* data, size_t len);
    /// Write the digest to a 32-byte buffer; subsequent updates remain possible.
    void digest(uint8_t out[kSha256DigestSize]) const;
private:
    uint32_t state_[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                          0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    uint8_t tail_[64] = {};
    uint64_t bytes_ = 0;
    size_t held_ = 0;
};

/// Hash `len` bytes at `data` into `out`, which must hold 32 bytes.
void sha256(const void* data, size_t len, uint8_t out[kSha256DigestSize]);

/// Hash `len` bytes and write the first `outBytes` of the digest as lowercase hex into `out`, which must hold `outBytes * 2 + 1` characters.
void sha256Hex(const void* data, size_t len, char* out, size_t outBytes = 16);

}  // namespace mm
