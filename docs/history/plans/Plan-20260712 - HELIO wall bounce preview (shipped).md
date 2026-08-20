# HELIO wall bounce preview

## Goal

Match the physical HELIO reference more closely: broad warm light fills the lower glass, reflected arcs retain soft halos, and a nearby wall receives light and returns a low-frequency color fill.

## Design

- Keep glass glow independent from the environment so free-hanging installations remain luminous.
- Add an optional wall environment behind the object. The wall receives a broad LED-color spill and contributes a softer warm bounce through the glass.
- Derive low-frequency fill from the RMS color of the current LED frame, while direct strip and reflection cores continue to use the full LED texture.
- Expose one persisted Wall checkbox that applies consistently to main, floating, card, and canvas-fallback previews.
- Keep the renderer analytical and allocation-free per frame apart from the existing LED texture upload.

## Verification

- Pin glow, wall-toggle propagation, and fallback behavior with JavaScript tests.
- Build the helio1 image and flash it over OTA.
- Compare live browser captures against frames sampled from the supplied video, including a wall-off check.

## Outcome

The renderer keeps the glass transparent, warms neutral light to 3000 K, broadens reflection halos, narrows the equatorial rim, and optionally adds wall spill and bounce. The wall setting persists and applies to every preview surface. Browser checks cover both wall states and the floating preview; the final firmware runs on the HELIO controller.
