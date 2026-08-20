# HELIO Sphere Optics

## Goal

Add a compact HELIO playlist whose native 2D fields use the circular LED geometry and the sphere's reflections as part of the composition. Restore the native Rings scene mapping and leave the migrated Static and Audio playlists intact.

## Design

- Keep one data-driven scene catalog and build a five-scene playlist from the restored native Ripple plus four new scenes: Orbiting Lobes, Harmonic Halo, Liquid Mirror, and Twin Suns. Reusing Ripple as the caustic-ring scene avoids duplicate effect types and extra scene-identity state.
- Use the existing `RingsEffect`, `LavaLampEffect`, `MetaballsEffect`, `DistortionWavesEffect`, and `PlasmaEffect` modules. Their textbook scalar and wave fields already operate in two dimensions, so HELIO's `RingLayout` samples them around the physical circumference without a product-specific render engine.
- Give each scene tuned controls and an exact browser preview driven by the same geometry and controls.
- Add a repeating `Sphere Optics` playlist without changing the saved Static or Audio playlist order.
- Map Ripple to `RingsEffect`, matching the native effect selected by a fresh HELIO build.
- Keep audio readiness data-driven per scene rather than relying on preset-number ranges.

## Verification

- Pin catalog membership, playlist order, native type routing, audio classification, and visible preview variation in the JavaScript tests.
- Build the desktop target with warnings as errors.
- Run `ctest --output-on-failure` and `uv run moondeck/scenario/run_scenario.py`.
- Build and flash HELIO, then verify every new scene changes live, remains responsive, and sustains the normal render rate.

## Boundaries

- No new effect class or product-specific hot-path code.
- No changes to the physical layout, reflection renderer, or existing migrated scene algorithms.
- No commit, push, or lifecycle gate list without a separate product-owner request.
