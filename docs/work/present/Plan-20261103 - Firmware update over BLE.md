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
- **`info`** adds `otaTarget` (uint32), `securityVersion` (uint32) and `otaWindow` (4096), next to the existing `otaSlotBytes`.
- **`ota-begin`**
  - Params: `$claims` (120 hex characters, the 60 claim bytes) and `$sig` (128 hex characters, the 64-byte IEEE P1363 signature).
  - Reply: `total` and `window`.
  - Errors: `ota-busy` (another update holds the gate), `ota-unauthorized` (bad encoding, target, size, version or signature), `ota-slot` (no inactive slot, or too small) and `ota-io`.
- **The image streams on its own characteristic.** One request and reply per 288-byte chunk measured 2.6 KB/s on the S3 at an ATT MTU of 517 (19 minutes for 3 MB): every chunk spent about seven connection intervals. The image therefore moves the way Nordic DFU and Espressif's BLE OTA move it, written without response and acknowledged per window. `ota-write` goes; RPC2 keeps the signed begin, end, abort and status.
  - **Data characteristic** `bee30704-…` (write without response, notify), in the MoonLight service beside state, command and identity.
  - **A packet** is `[u32 LE offset][bytes]`, the bytes filling the rest of one ATT write (MTU − 7). Bit 31 of the offset asks for an acknowledgement once the controller has stored everything up to this packet's end.
  - **An acknowledgement** is a notification of `[u32 LE received][u32 LE asked][u8 state]`: the bytes durably written, the end the answered request named, and `state` 0 while receiving or 1 once the transfer failed (an `ota-status` names why). The phone ignores an answer naming another end: it is a late answer to an earlier request, which otherwise costs every later window a resend. A failure ends the transfer, so it counts whatever it names.
  - **Flow control:** the phone sends one window (`window` bytes, 4096, from `ota-begin` and `info`), its last packet asking for an acknowledgement, and waits for it. An acknowledgement short of the window's end makes the phone resend from `received`.
  - **The controller** keeps, between the BLE host task and the render loop, an 8 KB byte ring allocated only while a transfer runs. A packet continuing what it holds is appended, the part it already holds trimmed; a gap or a full ring is dropped and acknowledged at once, so the phone resends from there. A packet from any link but the owner's is dropped. The render loop appends the ring to the slot, as `ota-write` did.
  - **Owner, timeout and abort** stay as below: a transfer belongs to the session that began it, and 30 s without data abandons it.
- **`ota-end`**
  - The transfer digest is verified, then the backend commits (`esp_ota_end` and the boot partition).
  - Reply: `rebooting:true`. The controller restarts 1.5 s after publishing the reply.
  - Errors: `ota-incomplete`, `ota-integrity` and `ota-io`.
- **`ota-abort`:** stops the calling session's transfer.
- **`ota-status`:** `phase` (`idle`, `uploading`, `committed`, `aborted` or `failed`), plus `received` and `total` for the calling session.
- **Ownership:** a transfer belongs to the RPC session that began it. Disconnecting aborts it (no resume), and so do 30 s without data. One transfer at a time device-wide: the gate also blocks HTTP, URL and MQTT updates.

## App

- The identity parser accepts both values of `bleOta`.
- The RPC client gains the five operations, each with its own parameter checks.
- A MoonLight BLE adapter implements `UpdatePorts` for the existing `FirmwareUpdate` runner:
  - `preflight` comes from `identity` and `info`;
  - `authenticate` binds the bundled signed claims to the bundled bytes and the controller's target. The signature itself is the device's to verify; the App holds no key;
  - `begin` and `commit` map onto the operations, and `write` streams one window and waits for its acknowledgement;
  - `reconnect` confirms the new build from `identity`.
- Bundled releases carry their claims and signature. The settings screen offers the update when the bundled release for the controller's variant differs from its running build.

## Done when

- Desktop tests cover begin, streaming, end, abort, owner, gaps, duplicates, a full ring, timeout, unauthorized and busy.
- An App test drives the runner through the adapter.
- On the S3 bench board, a signed image installs over BLE from the Mi9 in minutes rather than the 19 the request-per-chunk transfer took, and the controller then reports the new build. Met on 3 November 2026: 5cc7a498 streamed in about 3 minutes 50 seconds (13 KB/s, MTU 517, 15 ms interval).
- A tampered signature, a wrong target and an older security version are refused before the slot opens.

## Not in this plan

- Bootloader app rollback: it needs a USB-flashed bootloader.
- Key rotation beyond one key.
- Persistent anti-rollback.
