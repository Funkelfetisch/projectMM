# HELIO playlist scene galleries

## Intent

Make the two HELIO playlists the only scene catalog on the product dashboard. Each playlist owns its full scene previews, so the same animation is not shown once in a standalone gallery and again as a text-only playlist step.

## Plan

1. Specify playlists as full-width sections with Play/Stop controls and a two-column grid of complete scene cards.
2. Reuse one scene-card renderer for playlist entries, including active, audio-required, preview, label, metadata, and manual activation behavior.
3. Remove the standalone scene panel and aggregate playlist preview path.
4. Keep preview work viewport-gated and collapse each scene grid to one column on narrow screens.
5. Pin the structure in host-side UI tests, inspect it in a browser, then build the HELIO firmware and attempt the requested OTA deployment.

## Boundaries

- Playlist order and scene definitions remain the data source in `helio-scenes.js`.
- Device-side effect implementations and playlist timing do not change.
- Presets, active-scene controls, setup, and the floating preview remain in place.
