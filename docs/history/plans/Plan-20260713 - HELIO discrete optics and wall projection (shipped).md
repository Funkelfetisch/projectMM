# HELIO discrete optics and wall projection

## Goal

Make the controller preview preserve visible individual LEDs and clean mirror-line reflections while matching the production HELIO sphere's subtle glass shell. When a wall is enabled, project the live emitter pattern onto it so moving lights produce moving wall spots instead of an average-color background wash.

## Reference

- The live `PreviewDriver` frame remains the sole source for the large preview.
- The production sphere on `helio.lighting` supplies the renderer parameter baseline.
- The supplied physical video and browser screenshots define the optical acceptance check.

## Implementation

1. Inspect the production sphere implementation and record the geometry, material, glow, camera, and post-processing values that affect appearance.
2. Remove derivative-based reflection antialiasing from divergent shader control flow and use a deterministic world-space pixel width.
3. Keep direct emitters discrete and make reflection blur optically narrow; dark LEDs stay direct-only and never enter reflections.
4. Add a subtle shell thickness through refraction and rim response without adding interior haze.
5. Replace the average ambient wall gradient with a low-resolution angular light projection derived from the current LED frame.
6. Verify static and moving patterns in the browser, run the JavaScript tests, build `helio1`, and OTA-flash `10.0.0.87`.

## Acceptance

- Individual LEDs remain identifiable in the direct strip.
- Reflection energy stays on clean reflection lines with no dithered speckle.
- A moving light produces a moving wall spot when Wall is enabled.
- Wall disabled removes the projected spill.
- Glass reads as thin and clear rather than foggy.
- The controller remains responsive while visible cards animate.
