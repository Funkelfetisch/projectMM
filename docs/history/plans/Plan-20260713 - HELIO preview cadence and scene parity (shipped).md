# HELIO preview cadence and scene parity

## Intent

Make scene-card motion read like the native HELIO effects instead of jumping under browser load. Lake, Chase, and every preview that uses the shared 8-bit wave helpers follow the same fixed-point timing primitives as the C++ renderer.

## Plan

1. Match the browser preview's byte sine, beat, triangle, and cubic-wave arithmetic to `math8.h`.
2. Give each visible card a monotonic scene clock whose catch-up step is bounded after a delayed frame.
3. Render one two-column row per browser frame and target 30 FPS while retaining viewport-only rendering and the existing high-resolution backing store.
4. Migrate the previous default cadence once without overriding user-tuned values.
5. Pin the math and scheduling behavior in host-side tests, inspect the live page, build HELIO, and flash the controller.

## Boundaries

- Native effect implementations and device playback do not change.
- The optical model, reflection settings, and card resolution remain unchanged.
- Active-scene previews continue to use the true LED frame stream.

## Outcome

The fixed-point preview clock and viewport-bounded 30 FPS scheduler ship in UI build `20260713b`. The JavaScript suite passes 107/107 tests, and the live controller renders only visible scene cards while Lake and Chase select their native effect modules.
