# HELIO off LED reflections and browser cadence

## Goal

Keep inactive LED packages faintly visible without letting them emit reflected light, and remove the once-per-second browser animation hitch while preserving the full HELIO optical renderer.

## Plan

1. Encode physical package visibility separately from active emission in the HELIO LED texture. Direct inactive packages render at 30% of their current brightness; reflected paths, wall spill, and rim reflections sample active emission only.
2. Apply the same direct-versus-reflected rule to the canvas fallback.
3. Keep the one-second compact state update away from hidden generic module controls and avoid using state pushes to redraw a continuously running trace.
4. Run the main and floating trace at 30 FPS, stop the obscured main trace while the floating preview is open, and pause scene-card rendering behind the floating preview.
5. Pin the behavior in JavaScript tests, build `helio1`, flash `10.0.0.87`, and verify the rendered result and animation cadence in the browser.
