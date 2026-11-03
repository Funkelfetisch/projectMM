// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/util/FirmwareSignature.h"
#include <psa/crypto.h>

namespace mm {
bool verifyFirmwareSignatureP256(const uint8_t* publicKey, size_t publicKeySize,
                                 const uint8_t* digest, size_t digestSize,
                                 const uint8_t* signature, size_t signatureSize) {
    if (!publicKey || publicKeySize != 65 || publicKey[0] != 0x04 ||
        !digest || digestSize != 32 || !signature || signatureSize != 64) return false;
    if (psa_crypto_init() != PSA_SUCCESS) return false;
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_VERIFY_HASH);
    psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, 256);
    psa_key_id_t key = 0;
    psa_status_t imported = psa_import_key(&attributes, publicKey, publicKeySize, &key);
    psa_reset_key_attributes(&attributes);
    if (imported != PSA_SUCCESS) return false;
    const psa_status_t verified = psa_verify_hash(key, PSA_ALG_ECDSA(PSA_ALG_SHA_256),
                                                   digest, digestSize, signature, signatureSize);
    const psa_status_t destroyed = psa_destroy_key(key);
    return verified == PSA_SUCCESS && destroyed == PSA_SUCCESS;
}
} // namespace mm
