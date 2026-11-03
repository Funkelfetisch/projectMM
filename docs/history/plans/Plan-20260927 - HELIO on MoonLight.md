# HELIO 1 on MoonLight

## Decision

Build HELIO 1 on MoonModules/MoonLight `c57ff4bde77032cc34fe2ffd58c1d1e962212281` in an isolated worktree. Keep the existing HELIO branch as the reference. Carry a product difference only when a current upstream capability fails the HELIO requirement or measured behavior.

## Steps

1. Establish the unmodified MoonLight ESP32-S3 and host test baseline. Record toolchain, target, artifact, and failures separately from device behavior.
2. Implement the smallest HELIO 1 board and boot profile: ESP32-S3, 8 MB flash without PSRAM, 222 GRBW lights on GPIO 35, persisted 311-light installations, local microphone and BH1750, and product identity.
3. Compare output behavior in source and tests: explicit W, warm-white preview, same-tick scene replacement, and ESP32-S3 RMT ownership and cache safety. Use upstream primitives where equivalent; port only demonstrated gaps.
4. Compare each saved HELIO scene by behavior and defaults against MoonLight's effects. Retain a dedicated port where upstream output differs; use native upstream effects where parity is established. Adapt the HELIO UI to the current state and preview protocol.
5. Run targeted unit, scenario, host, and firmware builds, then record the resulting source and artifact hashes. Keep hardware acceptance separate: authorized installation, boot/state readback, constant RGBW and scene tests, and at least 20 minutes of visual observation with normal control and persistence activity.

No device flash, release, order, commit, or push is included in this work.
