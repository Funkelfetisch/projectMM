# HELIO center emitter wall projection

## Goal

Make wall illumination visibly originate inside the infinity sphere instead of reading as a luminous patch attached to the wall.

## Design

- Treat the sphere center as the single world-space source position.
- Derive wall direction, 110-degree spread, incidence, and distance falloff from the vector between that source and each wall point.
- Use the center-to-wall distance, including the configured wall gap, rather than the former full-diameter approximation.
- Render a restrained source radiance inside the clear sphere from the same LED/reflection emission used by the wall projection, so the source and its wall light remain visually connected.
- Preserve the moving angular color projection and the equatorial rim shadow.

## Verification

- Pin source geometry and the shared source/wall emission in JavaScript tests.
- Inspect sparse and continuous scenes in the browser with Wall enabled and disabled.
- Build `helio1`, upload to `10.0.0.87`, and verify the embedded UI and controller runtime.

## Outcome

The wall shader traces every projection ray from the sphere center and maps it back to the inward-facing strip in the sphere's x/z equator. Vertical spread follows the 110-degree emission cone; wall incidence, inverse-square distance falloff, and the rim shadow follow the same center-to-wall ray. This produces moving wall spots instead of wall-space radial sectors. UI version `20260713l` passes the JavaScript preview tests and runs on `10.0.0.87`.
