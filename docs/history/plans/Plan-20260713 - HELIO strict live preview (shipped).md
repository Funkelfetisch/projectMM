# HELIO strict live preview

## Intent

Make every HELIO surface presented as live render only the binary LED frames emitted by `PreviewDriver`. Missing or stale device data shows the unlit physical sphere instead of a JavaScript scene approximation, and every nonzero live channel remains emissive so low-brightness and network-received pixels stay visible.

## Plan

1. Specify the source boundary for the main, floating, active-scene, and inactive-scene previews.
2. Remove the JavaScript fallback from the live sampler and clear the cached frame when the WebSocket disconnects.
3. Use an exact-zero off threshold for device frames while retaining the tunable threshold for inactive simulated cards.
4. Pin source selection and arbitrary composed-buffer streaming in JavaScript and C++ tests.
5. Build HELIO, flash `10.0.0.87`, and verify Lake and the live frame indicators in a browser.

## Boundaries

- Inactive scene cards keep their browser simulation so the page does not run every native effect on the controller.
- The native effect implementations, optical controls, and physical output pipeline do not change.
- `NetworkReceiveEffect` needs no preview-specific branch: its output reaches `PreviewDriver` through the shared composed layer buffer.

## Outcome

UI build `20260713d` consumes `PreviewDriver` frames for the main, floating, and active-scene previews, treats only exact RGB zero as off, and redraws full traces from delivered frames. The JavaScript suite passes 107 tests, the HELIO firmware builds and runs on `10.0.0.87`, and browser diagnostics show 311 fresh device pixels with rendered and incoming maxima aligned. A composed-network-frame unit test pins incoming data through `PreviewDriver`.
