# HELIO Wi-Fi install under memory pressure

On the bench HELIO 1 (ESP32-S3, no PSRAM) running `ae3e7480`, installing an image through `POST /api/firmware/url` fails most attempts. The download task needs a 12 KB stack and `esp_https_ota` a 32 KB receive buffer, while the largest free internal block is 31 KB right after boot, 28 KB a minute later and 9 to 21 KB with BLE clients connected.

Measured on 4 November 2026, one install of a 2,813,840-byte `helio1` image, the signal at -52 dBm:

| Attempt | Largest free block | Outcome |
|---|---|---|
| 1 | 9 KB | `task create failed` |
| 2 | 31 KB, just booted | panic about 132 KB in (`bootReason=PANIC`, `kind=4`); rebooted into the running image |
| 3 | 28 KB | `ota begin ESP_ERR_NO_MEM` |
| 4 | 31 KB, just booted | stalled at 1,471,523 bytes |
| 5 | 31 KB, just booted, a phone driving BLE | `ota begin ERROR` |

An install the same afternoon succeeded on the second try. The panic's backtrace (`pc=40387ec0,40387e8c,40386b15,421c3d9d,…`) is not decoded, since its image's ELF is not kept.

The textbook remedy is an install that needs no large contiguous block: a receive buffer sized to what is free, or the BLE transfer's 4 KB window, which `bleOta:true` already offers on this firmware. Pin the outcome on hardware before calling it fixed.
