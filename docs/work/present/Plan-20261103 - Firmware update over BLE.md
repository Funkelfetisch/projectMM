# Plan: firmware update over BLE

NEBULITE garments run BLE-only by default, so their next firmware must be able to arrive over Bluetooth. The product owner asked for it on 3 November 2026. This plan wires the existing transport-neutral pieces (`FirmwareUploadSession`, `FirmwareAuthorizedBackend`, `FirmwareReleasePolicy`, `FirmwareOtaPort`, `FirmwareUpdateGate`) into RPC2, and the App's existing `FirmwareUpdate` runner onto it. See [BLE firmware transfer integration](../future/BLE%20firmware%20transfer%20integration.md) for the groundwork.

## Trust

- Open lighting access never authorizes an update: every transfer needs a release signed with the publisher's P-256 key. The device verifies it with `FirmwareReleasePolicy` before the slot opens.
- The public key is compiled into product firmware (`src/helio/FirmwarePublisherKey.h`, generated). The private key lives outside every repository, in `~/.local/state/nebulite-release-keys/firmware-p256.pem` (mode 0600). `moondeck/helio/firmware_signing.py` creates it and signs images.
- Signed claims are the existing 60-byte payload: `HELIO-FW-AUTH-V1`, then big-endian target, image size and security version, then the image SHA-256.
- The target is the FNV-1a 32-bit hash of the firmware variant name (`kFirmwareName`), so one rule needs no table.
- The minimum security version is the running firmware's own (`kFirmwareSecurityVersion`, starting at 1). This blocks downgrades below the running release, not persistently.
- The transfer SHA-256 still guards integrity. ESP-IDF's `esp_ota_end` verifies the image, and the first 4 KB flush checks chip, project and variant.

## Wire (RPC2, JSON, 512-byte requests)

- **Identity:** `bleOta` is true when the build has OTA hardware and a publisher key. `ota` stays `http-app`.
- **`info`** adds `otaTarget` (uint32), `securityVersion` (uint32) and `otaChunk` (288), next to the existing `otaSlotBytes`.
- **`ota-begin`**
  - Params: `$claims` (120 hex characters, the 60 claim bytes) and `$sig` (128 hex characters, the 64-byte IEEE P1363 signature).
  - Reply: `total` and `chunk`.
  - Errors: `ota-busy` (another update holds the gate), `ota-unauthorized` (bad encoding, target, size, version or signature), `ota-slot` (no inactive slot, or too small) and `ota-io`.
- **`ota-write`**
  - Params: `$offset` (equal to the bytes received so far) and `$data` (standard base64 with padding, 1 to 288 bytes).
  - Reply: `received`.
  - Errors: `ota-owner` (this session holds no transfer), `ota-offset` (the reply still carries `received`, so the App resynchronises), `ota-invalid` and `ota-io`. An `ota-io` here includes an image rejected at its first 4 KB flush.
- **`ota-end`**
  - The transfer digest is verified, then the backend commits (`esp_ota_end` and the boot partition).
  - Reply: `rebooting:true`. The controller restarts 1.5 s after publishing the reply.
  - Errors: `ota-incomplete`, `ota-integrity` and `ota-io`.
- **`ota-abort`:** stops the calling session's transfer.
- **`ota-status`:** `phase` (`idle`, `uploading`, `committed`, `aborted` or `failed`), plus `received` and `total` for the calling session.
- **Ownership:** a transfer belongs to the RPC session that began it. Disconnecting aborts it (no resume), and so do 30 s without a write. One transfer at a time device-wide: the gate also blocks HTTP, URL and MQTT updates.

## App

- The identity parser accepts both values of `bleOta`.
- The RPC client gains the five operations, each with its own parameter checks.
- A MoonLight BLE adapter implements `UpdatePorts` for the existing `FirmwareUpdate` runner:
  - `preflight` comes from `identity` and `info`;
  - `authenticate` binds the bundled signed claims to the bundled bytes and the controller's target. The signature itself is the device's to verify; the App holds no key;
  - `begin`, `write` and `commit` map onto the operations;
  - `reconnect` confirms the new build from `identity`.
- Bundled releases carry their claims and signature. The settings screen offers the update when the bundled release for the controller's variant differs from its running build.

## Done when

- Desktop tests cover begin, write, end, abort, owner, offset, timeout, unauthorized and busy.
- An App test drives the runner through the adapter.
- On both bench boards, a signed image installs over BLE from the Mi9, and the controller then reports the new build.
- A tampered signature, a wrong target and an older security version are refused before the slot opens.

## Not in this plan

- Bootloader app rollback: it needs a USB-flashed bootloader.
- Key rotation beyond one key.
- Persistent anti-rollback.
