# Migrating a device to MoonLight over the air

A device running another firmware moves to MoonLight over WiFi, through that firmware's own update page, with no cable and no opening the case.
It keeps its WiFi network, and usually its address.

The file you upload is the migration image, a small program that runs once.
It writes MoonBase (MoonLight's installer) and MoonLight's partition table and restarts into MoonBase, all in about ten seconds, and from WLED it carries the WiFi network over.
MoonBase then installs MoonLight from the release.

<img src="../assets/deviceModels/esp32-c3-rgbww-pwm-bulb.png" width="140" alt="An RGBWW PWM bulb with an ESP32-C3 inside, such as the Athom 12W: a WLED bulb that moves to MoonLight this way">

## What can move

| From | Chip | Status |
|---|---|---|
| WLED | ESP32-C3 with 4 MB of flash | verified on WLED 0.15.0, other versions should work; the WiFi comes along |
| WLED, the 2025 NEBULITE S3 garments and HELIO 1 | ESP32-S3 with 8 MB of flash | built, not yet run on a board. The image installs only its own product's signed MoonLight, which the phone posts to it ([below](#the-s3-migration-image-over-http)), and ends on MoonLight's dual-OTA table |
| Another firmware, such as ESPHome or Tasmota | ESP32-C3 with 4 MB of flash | untested. It needs the old firmware to accept the upload, and the image to land clear of where MoonBase goes or find a second app slot to move into. MoonBase starts on its access point |

A migration image is built for one chip, so the chip decides which file fits.

## Steps, with WLED as the example

[Move a WLED device to MoonLight, without a cable](../moonwalk.md#migrate-wled). The device catalog, [`deviceModels.json`](https://github.com/MoonModules/MoonLight/blob/main/mooninstaller/deviceModels.json), holds the settings of every board MoonLight knows, such as the **ESP32-C3 RGBWW PWM bulb**.

## What can stop it

Before it writes anything, the migration image checks that it can finish: enough flash, no flash encryption or secure boot, room to move itself out of the way.
When a check fails, it restarts the old firmware with nothing changed.
So a device that cannot move keeps working as it was:

- The old firmware refuses the upload. A newer WLED checks an upload's release name until its option to skip that check is ticked, and an update password or a locked update refuses it too.
- The migration image refuses the board, and the old firmware comes back.
- The WiFi is not where the image looks, and MoonBase opens its access point.

One moment needs care: for about 50 milliseconds in the middle, the image rewrites the partition table.
A power cut in exactly that window leaves the device needing a USB cable to recover, which on a bulb means opening it.
A power cut at any other moment comes back on its own: the image starts over, or MoonBase takes over.

Going back to the old firmware also needs a USB cable, since MoonBase installs MoonLight only.

**The migration is at your own risk.**
It is tested on the devices named above, and another device can differ from them in ways the image cannot check.
A device that needs a cable may have to be opened, which can void its warranty.

## The S3 migration image over HTTP

The S3 image carries no MoonLight of its own: the phone that installed it posts the product's signed release to it in chunks and then commits. Schema `nebulite-migrate/1`, for an app that drives the migration:

- **Finding it.** After WLED installs the image and restarts, the image answers where WLED answered:
  - it joins WLED's network under WLED's mDNS name, announcing `_http._tcp` and `_nebulite-migrate._tcp` on port 80, and the router usually gives it WLED's address again;
  - it always opens WLED's access point, with WLED's name and password, at `4.3.2.1`.
- **`GET /migrate`** answers the status:
  - `schema`, `build`, `chip` (`esp32s3`), `flashBytes`, and `mac`, the Wi-Fi station MAC as WLED's `/json/info` reports it;
  - `variant` (`nebulite-s3` or `helio1`), `stage`, `slotBytes`, `maxChunk`, `size` and `received`;
  - `power` (`usb`, `batteryPct`, `ok`), as measured at boot while the radio was still down;
  - `error`, the last failure's code.

  `stage` is `ready`, `receiving`, `received`, `committing` or `failed`.
- **`POST /migrate/begin`** with `{"size":n,"sha256":"<64 hex>","claims":"<120 hex>","signature":"<128 hex>"}`, from the release manifest. The image accepts a release signed for its own product, at least its compiled security version, whose size and SHA-256 the claims name. It answers the status. The same release as the upload under way resumes it, and any other starts from 0.
- **`POST /migrate/chunk?offset=n`** with raw bytes, 1 to `maxChunk`:
  - every chunk but the last fills whole 4 KB sectors;
  - the next contiguous chunk is written and read back, answering `{"received":n}`;
  - a chunk already held answers the same, so a retry after a lost reply is harmless;
  - any other offset answers 409 `{"error":"offset","received":n}`, the count to continue from.

  After the last byte, the image checks the release, and answers 422 `sha256` when it fails.
- **`POST /migrate/commit`** answers 202 `{"stage":"committing"}` before the table write. The image then finishes on its own, restarting twice in about twenty seconds. Keep the device powered and leave its buttons alone.

Errors are JSON `{"error":"<code>","received":n}`:

| Code | Status | From |
|---|---|---|
| `body` | 400 | begin |
| `size` | 400 | begin |
| `target` | 400 | begin |
| `signature` | 400 | begin |
| `offset` | 400 or 409 | chunk |
| `length` | 400 | chunk |
| `state` | 409 | chunk, commit |
| `busy` | 409 | begin |
| `timeout` | 408 | chunk |
| `flash` | 500 | begin, chunk, commit |
| `sha256` | 422 | the last chunk |
| `power` | 503 | begin, commit |

**The end.** MoonLight starts from its second slot, with the network WLED knew, and keeps the board's factory MAC. It is then found as any MoonLight is: over BLE by its advertisement, and on Wi-Fi when its Wi-Fi is on.

**After a cut.** A power cut restarts the image:
- an upload under way begins again;
- a release already received waits for the app to commit it again;
- a committed one finishes on its own.

A battery that is too low stops the image before it writes anything, and WLED comes back; once WLED's slot is gone, the image retries every 30 seconds instead.
