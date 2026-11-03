#!/usr/bin/env python3
"""Exercise the PSA signature primitive using generated, disposable keys.

Requires Python cryptography, a C++ compiler, and host PSA development headers
matching libmbedcrypto. Pass the include directory as the first argument.
This test does not establish release signing or an OTA policy.
"""
import hashlib
import pathlib
import struct
import subprocess
import sys
import tempfile

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, utils

root = pathlib.Path(__file__).resolve().parents[2]
key = ec.generate_private_key(ec.SECP256R1())
other = ec.generate_private_key(ec.SECP256R1())
public = key.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
wrong = other.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
image = b"disposable HELIO firmware signature test"
digest = hashlib.sha256(image).digest()
der = key.sign(digest, ec.ECDSA(utils.Prehashed(hashes.SHA256())))
r, s = utils.decode_dss_signature(der)
signature = r.to_bytes(32, "big") + s.to_bytes(32, "big")
claims_hash = hashlib.sha256(b"HELIO-FW-AUTH-V1" + struct.pack(">III", 0x11223344, len(image), 7) + digest).digest()
claims_der = key.sign(claims_hash, ec.ECDSA(utils.Prehashed(hashes.SHA256())))
cr, cs = utils.decode_dss_signature(claims_der)
claims_signature = cr.to_bytes(32, "big") + cs.to_bytes(32, "big")
signed_claims = b"HELIO-FW-AUTH-V1" + struct.pack(">III", 0x11223344, len(image), 7) + digest

def array(name, data):
    return f'uint8_t {name}[] = {{{", ".join(str(x) for x in data)}}};'

source = f'''#include "core/util/FirmwareSignature.h"
#include "core/util/FirmwareAuthorizedBackend.h"
#include "core/util/FirmwareReleasePolicy.h"
#include "core/util/FirmwareUploadSession.h"
#include <cassert>
#include <algorithm>
struct Flash {{
    int begins = 0, writes = 0, commits = 0, aborts = 0;
    size_t capacity() const {{ return 1024; }}
    bool begin(size_t) {{ ++begins; return true; }}
    bool write(const uint8_t*, size_t) {{ ++writes; return true; }}
    bool end(bool commit) {{ commit ? ++commits : ++aborts; return true; }}
}};
struct TestPolicy {{
    const uint8_t* key; const uint8_t* signature; size_t expectedSize;
    bool verify(size_t size, const std::array<uint8_t, 32>& digest) {{
        return size == expectedSize && mm::verifyFirmwareSignatureP256(key, 65, digest.data(), digest.size(), signature, 64);
    }}
}};
struct AbortFlash : Flash {{
    bool failAbort = true;
    bool abort() {{ ++aborts; return !failAbort; }}
}};
struct FailedEndFlash : AbortFlash {{
    bool end(bool) {{ ++commits; return false; }}
}};
int main() {{
    {array("key", public)}
    {array("other", wrong)}
    {array("digest", digest)}
    {array("signature", signature)}
    {array("claimsSignature", claims_signature)}
    {array("signedClaims", signed_claims)}
    {array("der", der)}
    {array("image", image)}
    mm::FirmwareReleaseClaims decoded;
    assert(mm::decodeFirmwareReleaseClaims(signedClaims, sizeof(signedClaims), claimsSignature, sizeof(claimsSignature), decoded));
    assert(decoded.target == 0x11223344 && decoded.imageSize == sizeof(image) && decoded.securityVersion == 7);
    assert(std::equal(decoded.imageDigest.begin(), decoded.imageDigest.end(), digest));
    assert(std::equal(decoded.signature.begin(), decoded.signature.end(), claimsSignature));
    assert(!mm::decodeFirmwareReleaseClaims(nullptr, sizeof(signedClaims), claimsSignature, sizeof(claimsSignature), decoded));
    assert(!mm::decodeFirmwareReleaseClaims(signedClaims, sizeof(signedClaims)-1, claimsSignature, sizeof(claimsSignature), decoded));
    assert(!mm::decodeFirmwareReleaseClaims(signedClaims, sizeof(signedClaims), claimsSignature, sizeof(claimsSignature)-1, decoded));
    signedClaims[0] ^= 1;
    assert(!mm::decodeFirmwareReleaseClaims(signedClaims, sizeof(signedClaims), claimsSignature, sizeof(claimsSignature), decoded));
    assert(decoded.target == 0x11223344);
    signedClaims[0] ^= 1;
    auto verify = mm::verifyFirmwareSignatureP256;
    assert(verify(key, sizeof(key), digest, sizeof(digest), signature, sizeof(signature)));
    assert(!verify(other, sizeof(other), digest, sizeof(digest), signature, sizeof(signature)));
    digest[0] ^= 1;
    assert(!verify(key, sizeof(key), digest, sizeof(digest), signature, sizeof(signature)));
    digest[0] ^= 1; signature[0] ^= 1;
    assert(!verify(key, sizeof(key), digest, sizeof(digest), signature, sizeof(signature)));
    signature[0] ^= 1;
    assert(!verify(key, sizeof(key), digest, sizeof(digest), der, sizeof(der)));
    assert(!verify(key, 64, digest, sizeof(digest), signature, sizeof(signature)));
    assert(!verify(key, sizeof(key), digest, 31, signature, sizeof(signature)));
    assert(!verify(nullptr, 65, digest, sizeof(digest), signature, sizeof(signature)));
    assert(!verify(key, sizeof(key), nullptr, 32, signature, sizeof(signature)));
    assert(!verify(key, sizeof(key), digest, sizeof(digest), nullptr, 64));
    for (unsigned i = 1; i < sizeof(other); ++i) other[i] = 0;
    assert(!verify(other, sizeof(other), digest, sizeof(digest), signature, sizeof(signature)));
    assert(verify(key, sizeof(key), digest, sizeof(digest), signature, sizeof(signature)));
    Flash flash;
    TestPolicy policy{{key, signature, sizeof(image)}};
    mm::FirmwareAuthorizedBackend<Flash, TestPolicy> backend(flash, policy);
    mm::FirmwareUploadSession session(backend);
    using Result = decltype(session)::Result;
    decltype(session)::Digest imageDigest{{}};
    std::copy_n(digest, sizeof(digest), imageDigest.begin());
    AbortFlash uncertainFlash;
    mm::FirmwareAuthorizedBackend<AbortFlash, TestPolicy> uncertainBackend(uncertainFlash, policy);
    mm::FirmwareUploadSession uncertainSession(uncertainBackend);
    using UncertainResult = decltype(uncertainSession)::Result;
    assert(uncertainSession.begin(1, sizeof(image), imageDigest) == UncertainResult::Ok);
    assert(uncertainSession.abort(1) == UncertainResult::Io);
    assert(uncertainSession.begin(2, sizeof(image), imageDigest) == UncertainResult::Invalid);
    assert(uncertainFlash.begins == 1);
    uncertainFlash.failAbort = false;
    assert(uncertainBackend.abort());
    assert(uncertainSession.begin(2, sizeof(image), imageDigest) == UncertainResult::Ok);
    assert(uncertainSession.abort(2) == UncertainResult::Ok);
    FailedEndFlash failedEndFlash;
    mm::FirmwareAuthorizedBackend<FailedEndFlash, TestPolicy> failedEndBackend(failedEndFlash, policy);
    mm::FirmwareUploadSession failedEndSession(failedEndBackend);
    using FailedEndResult = decltype(failedEndSession)::Result;
    assert(failedEndSession.begin(3, sizeof(image), imageDigest) == FailedEndResult::Ok);
    assert(failedEndSession.append(3, 0, image, sizeof(image)) == FailedEndResult::Ok);
    assert(failedEndSession.finish(3) == FailedEndResult::Io);
    assert(failedEndSession.begin(4, sizeof(image), imageDigest) == FailedEndResult::Invalid);
    assert(failedEndFlash.begins == 1);
    assert(!failedEndBackend.abort());
    failedEndFlash.failAbort = false;
    assert(failedEndBackend.abort());
    assert(failedEndSession.begin(4, sizeof(image), imageDigest) == FailedEndResult::Ok);
    assert(failedEndSession.abort(4) == FailedEndResult::Ok);
    assert(!backend.begin(sizeof(image)));
    assert(!backend.write(image, sizeof(image)));
    assert(!backend.end(true));
    imageDigest[0] ^= 1;
    assert(session.begin(1, sizeof(image), imageDigest) == Result::Invalid);
    assert(flash.begins == 0);
    imageDigest[0] ^= 1;
    assert(session.begin(1, sizeof(image) - 1, imageDigest) == Result::Invalid);
    assert(flash.begins == 0);
    assert(session.begin(1, sizeof(image), imageDigest) == Result::Ok);
    image[0] ^= 1;
    assert(session.append(1, 0, image, sizeof(image)) == Result::Ok);
    assert(session.finish(1) == Result::Integrity);
    assert(flash.commits == 0 && flash.aborts == 1);
    image[0] ^= 1;
    assert(session.begin(1, sizeof(image), imageDigest) == Result::Ok);
    assert(session.append(1, 0, image, sizeof(image)) == Result::Ok);
    assert(session.finish(1) == Result::Ok);
    assert(flash.commits == 1);
    mm::FirmwareReleaseClaims claims;
    claims.target = 0x11223344; claims.imageSize = sizeof(image); claims.securityVersion = 7;
    std::copy_n(digest, sizeof(digest), claims.imageDigest.begin());
    std::copy_n(claimsSignature, sizeof(claimsSignature), claims.signature.begin());
    std::array<uint8_t, 65> trustedKey{{}};
    std::copy_n(key, sizeof(key), trustedKey.begin());
    mm::FirmwareReleasePolicy release(claims, 0x11223344, 5, trustedKey);
    assert(release.verify(sizeof(image), imageDigest));
    assert(!release.verify(sizeof(image) - 1, imageDigest));
    assert(!mm::FirmwareReleasePolicy(claims, 0x11223345, 5, trustedKey).verify(sizeof(image), imageDigest));
    assert(!mm::FirmwareReleasePolicy(claims, 0x11223344, 8, trustedKey).verify(sizeof(image), imageDigest));
    auto altered = claims; altered.securityVersion = 8;
    assert(!mm::FirmwareReleasePolicy(altered, 0x11223344, 5, trustedKey).verify(sizeof(image), imageDigest));
    altered = claims; altered.target++;
    assert(!mm::FirmwareReleasePolicy(altered, altered.target, 5, trustedKey).verify(sizeof(image), imageDigest));
    altered = claims; altered.signature[0] ^= 1;
    assert(!mm::FirmwareReleasePolicy(altered, 0x11223344, 5, trustedKey).verify(sizeof(image), imageDigest));
    mm::FirmwareReleasePolicy badRelease(altered, 0x11223344, 5, trustedKey);
    Flash rejectedFlash;
    mm::FirmwareAuthorizedBackend<Flash, mm::FirmwareReleasePolicy> rejectedBackend(rejectedFlash, badRelease);
    mm::FirmwareUploadSession rejectedSession(rejectedBackend);
    using RejectedResult = decltype(rejectedSession)::Result;
    assert(rejectedSession.begin(2, sizeof(image), imageDigest) == RejectedResult::Invalid);
    assert(rejectedFlash.begins == 0);
    Flash signedFlash;
    mm::FirmwareAuthorizedBackend<Flash, mm::FirmwareReleasePolicy> signedBackend(signedFlash, release);
    mm::FirmwareUploadSession signedSession(signedBackend);
    using SignedResult = decltype(signedSession)::Result;
    assert(signedSession.begin(2, sizeof(image), imageDigest) == SignedResult::Ok);
    assert(signedSession.append(2, 0, image, sizeof(image)) == SignedResult::Ok);
    assert(signedSession.finish(2) == SignedResult::Ok && signedFlash.commits == 1);
    key[0] = 2;
    assert(!verify(key, sizeof(key), digest, sizeof(digest), signature, sizeof(signature)));
}}
'''
with tempfile.TemporaryDirectory(prefix="helio-signature-") as tmp:
    test = pathlib.Path(tmp) / "test.cpp"
    test.write_text(source)
    exe = pathlib.Path(tmp) / "test"
    subprocess.run(["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    "-I", sys.argv[1], str(test), str(root / "src/core/util/sha256.cpp"), str(root / "src/core/util/FirmwareSignature.cpp"),
                    "-l:libmbedcrypto.so.16", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("P-256 signature and authorized upload: valid, tampering and rejection passed")
