# HELIO preview tuning controls

## Context

Near-black effect values still appear as active point reflections because the renderer treats only exact zero as off. The optical defaults also live as source constants, so matching different wall and free-hanging installations requires code edits.

## Alternatives

- Raise a fixed brightness threshold: removes the current dots, but every later optical adjustment still requires rebuilding firmware.
- Expose every shader uniform: maximizes control, but leaks internal implementation details and makes the setup panel unwieldy.
- Expose the stable optical and quality controls, classify near-black samples as off, and cap reflected LED sources independently from direct LEDs: removes the artifact, preserves the visible strip, and keeps performance tunable.

## Choice

Use the third option. Persist browser-local preview settings, apply the same optical values to large and card previews, and keep separate reflection-count and resolution controls for large and card renderers.

## Work

1. Add off-threshold, off-brightness, reflected-source limit, attenuation, and cutoff parameters to both WebGL and canvas renderers.
2. Keep every LED in the direct strip while reflecting only the brightest configured number above the off threshold.
3. Add a folded Preview rendering section to HELIO Setup with resettable optical, wall, resolution, and cadence controls.
4. Pin normalization and reflection-source selection with JavaScript tests.
5. Inspect the result in the browser, build the HELIO firmware, OTA flash it, and verify the live device.
