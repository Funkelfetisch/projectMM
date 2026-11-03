# BLE firmware transfer integration

The `FirmwareUploadFrame`, `FirmwareUploadSession` and
`FirmwareUploadCoordinator` classes are experimental transport-independent
building blocks. They do not expose a BLE characteristic, authenticate a peer,
verify a publisher signature or prove that an installed image can recover.
RPC2 now wires them, behind a signed release, as the update over BLE: see [the plan](../present/Plan-20261103%20-%20Firmware%20update%20over%20BLE.md).

The existing begin frame is 38 bytes without a resume token and 54 bytes with
one. Neither fits in a single ordinary ATT write at the default MTU of 23
bytes (20 bytes of characteristic value). A future transport must explicitly
handle the negotiated payload size and agree on framing and acknowledgement
with the app; merely passing each 20-byte write directly to the current frame
parser will reject the begin operation. Reconnect, incomplete frames, timeout,
owner isolation, backpressure and ambiguous commit must be addressed in that
agreement. These observations do not establish a wire protocol or enable an
update path.

`FirmwareUploadAssembly` is a bounded, transport-neutral reassembly helper:
it accepts a caller-supplied total length and contiguous offsets, isolates
connection generations, and expires incomplete or unconsumed frames. The
transport must define its own fragment encoding, serialize access, release a
generation on disconnect, and dispatch a completed frame exactly once. This
helper neither establishes the app protocol nor performs authentication.
`FirmwareUploadFragmentBridge` joins this reassembly to the upload coordinator,
dispatches each completed assembly once and drops incomplete fragments on
disconnect. It is still transport-neutral and must not be advertised as an
available update endpoint.

After a successful digest check and backend commit, the in-memory upload
status retains the verified transfer SHA-256. It is absent on an incomplete,
aborted, hash-mismatched or failed commit. This can support an eventual
device-confirmed receipt, but the status is volatile: it does not survive a
reboot, authenticate the publisher, or prove that the new image booted.

The App's identity parser accepts both values of `bleOta`; the change is recorded in MIGRATING.
The current App controller's `info` parser was also exercised offline with an
unknown additive field: it still accepts the response and retains its existing
pixel-map capability. This only establishes a possible compatibility path;
the probe is not an advertised update capability or an agreed OTA protocol,
and it does not prove compatibility with other released App versions.
Image integrity via the transfer SHA-256 is not publisher authentication;
open lighting access must not by itself authorize arbitrary firmware updates.
Slot suitability, update authentication, rollback and recovery remain to be
qualified for each actual board before an update can be offered.

The isolated `nebulite-s3` build from 2026-10-29 is a concrete security
baseline, not a statement about any installed device: its generated sdkconfig
has `CONFIG_MBEDTLS_PK_C`, `CONFIG_MBEDTLS_ECDSA_C` and
`CONFIG_MBEDTLS_ECP_DP_SECP256R1_ENABLED`, but `CONFIG_SECURE_BOOT`,
`CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT` and
`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` are not set. The selected
`esp32/partitions/esp32s3_n8r8.csv` contains two 0x300000-byte OTA slots;
the latest snapshot has 0x332a0 bytes (7%) free in the smaller slot. The
existence of cryptographic primitives does not verify a release, provision a
trust anchor, enable bootloader rollback or prove that another board has this
partition table. Any verifier must be integrated with a reviewed signing and
key-rotation process, then built and measured against this limited space.

`src/core/util/FirmwareSignature.h` provides an isolated PSA P-256/SHA-256
signature primitive with a 65-byte uncompressed public point and 64-byte
P1363 signature. `moondeck/helio/check_firmware_signature.py` checks generated
test keys, invalid keys, tampering and malformed inputs against host
libmbedcrypto. A disposable S3 build also compiled a reference to the
primitive. It is not called by the update path; the compiler/linker may remove
the unused probe, so this build does not measure its final space cost. No
production key, signed manifest, signature policy, OTA transport or device
verification is established by this primitive.

The optional `FirmwareAuthorizedBackend` allows an upload session to check a
policy over its exact requested image length and digest before opening a slot.
The update over BLE enables it with `FirmwareReleasePolicy`. A host test combines this gate with the
PSA primitive and a simulated flash backend: an invalid signature or mismatched
length cannot open a slot, changed image bytes cannot commit, and the expected
image can. Its disposable test policy signs only the image digest and is not a
production release policy: target compatibility, version/rollback rules and
trust-anchor provisioning still require an agreed authenticated manifest.

An experimental `FirmwareReleasePolicy` signs a domain-separated, fixed-width
claim containing target, image size, security version and image SHA-256. Its
test verifies these claims against a disposable public key and checks altered
target, version, signature and length. The caller supplies the expected target,
minimum version and key; none is currently provisioned in product firmware.
In particular, comparing a supplied minimum version is not persistent
anti-rollback protection. This internal test format is not yet a released App
or transport contract and must be agreed before interoperability is claimed.
`moondeck/helio/check_app_signed_update.py` connects the real App orchestration
to that policy and the firmware fragment bridge through a simulated transport:
a disposable signed image transfers; a modified signature is rejected before
transfer and the App retains its recovery journal. The current App journal
passes the original signed bytes and signature to the simulated adapter, which
passes them to the firmware policy. The fixture's App-side verifier is mocked:
the App's textual board, variant and partition claims are not decoded from the
experimental firmware's numeric target, so this test does not prove that those
identities are cryptographically bound or mutually understood. This is not proof
of a working release provider, BLE endpoint, persistent rollback protection or
boot verification.

The offline adapter now passes the original signed bytes and detached signature
to `decodeFirmwareReleaseClaims`, rather than decoding them in the test harness.
It rejects wrong lengths and domain before producing claims; signature and
target validation remain separate policy steps. This fixed-width encoding is
experimental and does not establish a production wire or target-ID contract.

The ESP32 application upload port now exposes an explicit abort result. The
session records `Failed/Io` when it fails, and a failure of `esp_ota_abort`
retains the shared update gate until reboot. This also applies to cleanup on
other ESP32 write failure paths. Existing `otaEnd(..., false)` still reports
commit success and therefore returns false. The explicit abort confirms only
the ESP-IDF call, not erasure: partially written bytes may remain in the inactive
partition. A future transport must not equate it with verified discard. The
behavior and boot partition still require physical qualification; desktop and
Teensy conservatively report that their abort is unconfirmed.

A later isolated S3 measurement forces a call to the actual
`FirmwareReleasePolicy::verify` into the linked image via an artificial
`helio_ota_crypto_probe` symbol. The baseline is 2,936,528 bytes and the probe
image is 2,936,960 bytes (+432 bytes), leaving 0x32f80 bytes in the 0x300000
slot. The linker map contains the probe, `psa_import_key`, and
`psa_verify_hash`; some crypto code was already linked in the baseline. This
measures this policy/verifier probe only, not a BLE endpoint, release parser,
key provisioning, or the complete updater. The probe was added solely to the
isolated snapshot `/tmp/helio-s3-battery-current-eyaiaBRt` and is not an
installable release or device test.
