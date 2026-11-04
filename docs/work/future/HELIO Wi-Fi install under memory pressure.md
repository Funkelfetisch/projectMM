# HELIO Wi-Fi install: memory pressure, and nothrow new aborting

On the bench HELIO 1 (ESP32-S3, no PSRAM) running `ae3e7480`, installing an image through `POST /api/firmware/url` fails most attempts. The download task needs a 12 KB stack and `esp_https_ota` a 32 KB receive buffer, while the largest free internal block is 31 KB right after boot, 28 KB a minute later and 9 to 21 KB with BLE clients connected.

Measured on 4 November 2026, one install of a 2,813,840-byte `helio1` image, the signal at -52 dBm:

| Attempt | Largest free block | Outcome |
|---|---|---|
| 1 | 9 KB | `task create failed` |
| 2 | 31 KB, just booted | panic about 132 KB in (`bootReason=PANIC`, `kind=4`); rebooted into the running image |
| 3 | 28 KB | `ota begin ESP_ERR_NO_MEM` |
| 4 | 31 KB, just booted | stalled at 1,471,523 bytes |
| 5 | 31 KB, just booted, a phone driving BLE | `ota begin ERROR` |
| 6 | 31 KB, just booted, nothing connected | panic about 693 KB in, the same backtrace as attempt 2 |

An install the same afternoon, from 6.1.0-dev, panicked once and succeeded on the second try. Both panics on `ae3e7480`, attempts 2 and 6, record the same backtrace, decoded against a rebuild of that commit (2,811,920 bytes against the board's 2,811,856, the same managed components):

```
panic_abort ← abort ← __wrap___cxa_allocate_exception ← std::bad_alloc ← operator new
  ← operator new[] ← operator new[](std::nothrow) ← mm::ControlList::grow (Control.h:443)
```

A failed allocation aborts. `ControlList::grow` asks with `new (std::nothrow)` and is written to leave a control out when that returns null, but libstdc++'s nothrow `new` calls the throwing one inside a `try`, and with `CONFIG_COMPILER_CXX_EXCEPTIONS=n` ESP-IDF wraps `__cxa_allocate_exception` to `abort()`. So every `new (std::nothrow)` in the firmware, 29 of them under `src/`, crashes an ESP32 out of memory rather than returning null. The download's 12 KB stack and 32 KB buffer leave the next small allocation nothing; `grow` is the one the backtrace caught, and the frames above it are cut off at eight.

Two remedies, both by the book. The C++ standard lets a program replace the nothrow `operator new` and `operator new[]`; on ESP32, versions that return `malloc`'s result make the promise true, so a failed allocation degrades the way its caller is written to. And an install that needs no large contiguous block: a receive buffer sized to what is free, or the BLE transfer's 4 KB window, which `bleOta:true` already offers on this firmware. Pin the outcome on hardware before calling it fixed.
