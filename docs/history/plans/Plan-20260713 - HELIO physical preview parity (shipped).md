# HELIO physical preview parity

## Goal

Make the HELIO dashboard's large live preview represent the physical infinity sphere closely enough that the transmitted LED frame, warm-white output, clear acrylic, internal reflections, and optional wall bounce remain visually distinguishable from the scene-card simulator.

## Scope

1. Match the firmware's cyclic 16-entry palette interpolation in simulated scene previews.
2. Convert the physical RGBW output to preview RGB with the HELIO strip's 3000 K warm-white chromaticity.
3. Keep the direct LED strip discrete while deriving internal reflections from every lit LED through a spatially smoothed reflection signal; off LEDs contribute no reflected light.
4. Remove colored ambient fill from the sphere interior. Wall bounce remains an optional background contribution outside and through the clear shell.
5. Pin the color and reflection contracts in focused C++ and JavaScript tests, then verify the live result against the supplied physical video in the browser.

## Constraints

- The main and floating previews consume only device-streamed LED frames.
- Scene cards use firmware-equivalent simulation except for the active card, which may mirror the device frame.
- Rendering remains bounded for embedded-served browsers: no per-frame allocation in the C++ path, no extra WebGL renderer per card, and no texture wider than common WebGL limits.
- Firmware is built and flashed to `10.0.0.87` after verification.
