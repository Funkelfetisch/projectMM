# HELIO responsive scene switching

## Problem

Selecting a HELIO scene pauses output and the dashboard for several seconds. The browser currently performs a compact state fetch before every selection, may perform two full state fetches after replacing a scene type, sends the scene palette twice, deletes inactive scene modules one request at a time, waits explicitly, and performs another retried state fetch. Re-selecting the active scene also sends `enabled=true`, which triggers a full scheduler prepare even though the value is unchanged. The HELIO ring is also misclassified as a dense 50x50 grid: PreviewDriver sends 2,500 points instead of the ring's 311 logical lights, blocks the render loop for 15-30 ms per preview frame, and fills the preview with black points.

## Design

1. Treat the WebSocket-backed browser state as the current source for ordinary scene selections. A scene type already present needs no preparatory or final state fetch.
2. Return the newly created module resource from a structural replace and splice it into browser state. This keeps the generated name and control descriptors without fetching the complete module tree.
3. Send the scene palette once and only when it differs. Keep inactive modules disabled instead of deleting them in the interaction path; a scene click must not perform metadata cleanup.
4. Do not emit a target `enabled=true` operation when the target is already enabled. An empty activation plan means the requested scene is already active and is a success.
5. Preserve manual-selection playlist cancellation, queued clicks, audio setup, optimistic control rendering, and live-preview reset.
6. Treat a layout as a dense natural-order box only when the driver buffer contains exactly every box cell. A ring and any other sparse layout must walk its real coordinates even when it has no mapping LUT.

## Verification

- Browser orchestration tests pin no-op activation and replacement without a full-state refresh.
- PreviewDriver tests pin that the 311-light HELIO ring produces 311 coordinates and 311 colors, not the 2,500-cell bounding box.
- The desktop build, unit tests, and scenarios remain green.
- The HELIO ESP32 image builds and is flashed to `192.168.8.202`.
- Browser testing covers repeated selection, switching between existing scene types, switching to a type that requires replacement, and manual interruption of a playlist.
