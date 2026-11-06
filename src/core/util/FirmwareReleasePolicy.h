// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "core/util/FirmwareSignature.h"
#include "core/util/sha256.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace mm {
/// Fixed width of the domain, three big-endian claims fields, and image SHA-256.
inline constexpr size_t kFirmwareReleaseClaimsSize = sizeof("HELIO-FW-AUTH-V1") - 1 + 12 + 32;
/// Experimental internal signed claims; this is not an advertised wire format. The caller must obtain the immutable target, minimum version and publisher key from trusted device state, and must handle key rotation and rollback.
struct FirmwareReleaseClaims {
    uint32_t target = 0,           ///< The target the image is signed for.
             imageSize = 0,        ///< The image size in bytes.
             securityVersion = 0;  ///< The version a device compares against its minimum.
    std::array<uint8_t, 32> imageDigest{};
    std::array<uint8_t, 64> signature{};
};

/// Decode the experimental fixed-width signed payload and detached P-256 signature; decoding alone does not establish trust or authorize an upload.
inline bool decodeFirmwareReleaseClaims(const uint8_t* bytes, size_t size,
                                        const uint8_t* signature, size_t signatureSize,
                                        FirmwareReleaseClaims& out) {
    static constexpr uint8_t domain[] = "HELIO-FW-AUTH-V1";
    constexpr size_t domainSize = sizeof(domain) - 1;
    if (!bytes || !signature || size != kFirmwareReleaseClaimsSize || signatureSize != 64 ||
        !std::equal(domain, domain + domainSize, bytes)) return false;
    const auto read32 = [](const uint8_t* p) {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
               (uint32_t(p[2]) << 8) | uint32_t(p[3]);
    };
    FirmwareReleaseClaims decoded;
    decoded.target = read32(bytes + domainSize);
    decoded.imageSize = read32(bytes + domainSize + 4);
    decoded.securityVersion = read32(bytes + domainSize + 8);
    std::copy_n(bytes + domainSize + 12, decoded.imageDigest.size(), decoded.imageDigest.begin());
    std::copy_n(signature, decoded.signature.size(), decoded.signature.begin());
    out = decoded;
    return true;
}

/// Judges an image against signed claims, the device's own target and minimum version, and the publisher key.
class FirmwareReleasePolicy {
public:
    /// Binds the claims to the trusted target, minimum security version and publisher key they are judged against.
    FirmwareReleasePolicy(const FirmwareReleaseClaims& claims, uint32_t expectedTarget,
                          uint32_t minimumVersion, const std::array<uint8_t, 65>& publisherKey)
        : claims_(claims), expectedTarget_(expectedTarget), minimumVersion_(minimumVersion), key_(publisherKey) {}

    /// True when the image's size and digest match the claims, the target and version are acceptable, and the signature verifies under the publisher key.
    bool verify(size_t size, const std::array<uint8_t, 32>& digest) const {
        if (!expectedTarget_ || claims_.target != expectedTarget_ || !claims_.imageSize ||
            size > std::numeric_limits<uint32_t>::max() || size != claims_.imageSize ||
            claims_.securityVersion < minimumVersion_ || claims_.imageDigest != digest) return false;
        // Domain separation, then three big-endian 32-bit fields, then SHA-256 of the image; fixed widths avoid ambiguous concatenation.
        static constexpr uint8_t domain[] = "HELIO-FW-AUTH-V1";
        uint8_t fields[12];
        put32(fields, claims_.target);
        put32(fields + 4, claims_.imageSize);
        put32(fields + 8, claims_.securityVersion);
        Sha256 sha;
        if (!sha.update(domain, sizeof(domain) - 1) || !sha.update(fields, sizeof(fields)) ||
            !sha.update(claims_.imageDigest.data(), claims_.imageDigest.size())) return false;
        uint8_t signedDigest[32];
        sha.digest(signedDigest);
        return verifyFirmwareSignatureP256(key_.data(), key_.size(), signedDigest, sizeof(signedDigest),
                                           claims_.signature.data(), claims_.signature.size());
    }
private:
    static void put32(uint8_t* out, uint32_t value) {
        out[0] = static_cast<uint8_t>(value >> 24); out[1] = static_cast<uint8_t>(value >> 16);
        out[2] = static_cast<uint8_t>(value >> 8); out[3] = static_cast<uint8_t>(value);
    }
    FirmwareReleaseClaims claims_;
    uint32_t expectedTarget_, minimumVersion_;
    std::array<uint8_t, 65> key_;
};
} // namespace mm
