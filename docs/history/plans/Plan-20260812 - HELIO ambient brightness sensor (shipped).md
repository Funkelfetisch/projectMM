# HELIO ambient brightness sensor

## Problem

HELIO 1 has a BH1750FVI ambient-light sensor on I2C bus 1 (SDA GPIO26, SCL GPIO11, address 0x23), but the firmware neither reads it nor adapts LED brightness to the room. Writing each sensor-derived value into the persisted `Drivers.brightness` control would wear flash and erase the user's manual fallback level.

## Design

1. Add the standard persistent-I2C-device platform adapter: open one bus/device, write command bytes, read result bytes, and release it. ESP32 owns the ESP-IDF handles; desktop remains an inert stub.
2. Add `Bh1750Service` under `Services`. It runs the sensor in continuous high-resolution mode, converts the 16-bit result to lux using the datasheet's `raw / 1.2` transfer function, and publishes a transient ambient-brightness target.
3. Define the mapping with editable dark/bright lux anchors, independent brightness values at each anchor, and a lux hysteresis. Independent endpoint brightness allows either an increasing or decreasing response without another mode switch.
4. Keep `Drivers.brightness` as the persisted manual fallback. `Drivers` consumes the active service's transient target and rebuilds output correction only when that target changes; disabling/removing/failing the service returns immediately to manual brightness.
5. Wire one service into HELIO firmware with bus 1, SDA 26, SCL 11, and address 0x23. Surface the automatic toggle, current lux, both mapping anchors, and hysteresis in the folded HELIO Setup menu.

## Verification

- Unit tests pin raw-to-lux conversion, endpoint/intermediate mapping, reverse mapping, hysteresis, and manual fallback after disable.
- The desktop build, unit tests, scenarios, JavaScript tests, and ESP32 HELIO build pass.
- The HELIO image is flashed to `192.168.8.202`; the live service reports address 0x23, changing lux, and a stable automatically selected output brightness.
