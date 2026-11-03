// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

namespace mm {

/// Verify a SHA-256 digest with a P-256 public key using PSA Crypto.
/// Key: 65-byte uncompressed SEC1 point (04 || X || Y).
/// Signature: 64-byte fixed-width IEEE P1363 (r || s), not ASN.1 DER.
/// This primitive does not define a signed manifest or choose a trust anchor.
bool verifyFirmwareSignatureP256(const uint8_t* publicKey, size_t publicKeySize,
                                 const uint8_t* digest, size_t digestSize,
                                 const uint8_t* signature, size_t signatureSize);
} // namespace mm
