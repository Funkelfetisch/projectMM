# HELIO WiFi provisioning and upstream convergence

## Goal

Make HELIO provisioning stable on a new WiFi network, preserve the setup portal while somebody is using it, and rebase the complete HELIO branch onto current upstream without losing product-specific functionality.

## Scope

1. Reproduce provisioning over USB and capture the station disconnect reason without exposing credentials.
2. Preserve the dirty HELIO worktree, rebase its committed base onto `origin/next-iteration`, then reapply the uncommitted product work with upstream as the conflict-resolution baseline.
3. Keep only networking behavior that current upstream does not provide:
   - reconnect a dropped station from the platform event handler;
   - distinguish deliberate station shutdown from an unexpected disconnect;
   - track connected AP clients;
   - keep the setup AP alive while a client is connected;
   - stage SSID and password edits in the browser and send both complete values only when Connect is pressed.
4. Add focused tests for reconnect and AP-client gating, then run the applicable desktop, unit, scenario, and ESP32 checks.
5. Flash the USB-connected HELIO, provision it to the actual host SSID, and verify both LAN access and fallback AP behavior.
6. Audit the rebased HELIO delta and remove code that current upstream now provides.
7. Preserve the stale-pixel clear on effect-set changes while rendering the incoming scene in the same frame, avoiding visible black flashes during HELIO scene selection.
8. Keep page startup responsive when a client disconnects during state transfer: stream full snapshots through `GET /api/state`, use WebSocket only for bounded patches and resync notices, latch the first failed sink write, and migrate the accidental generic `RingsEffect` default back to the native HELIO ripple scene.
9. Replace the RMT driver's one-symbol-per-bit frame with ESP-IDF's standard byte encoder. Keep one corrected byte per physical channel, so a 222-light RGBW strand uses 888 frame bytes instead of 28,416 symbol bytes and can switch between RGB and RGBW without exhausting internal RAM.
10. Preserve an explicit fourth source channel while allowing `whiteMode` to derive white when that channel is zero. This keeps network RGBW input and Solid's explicit white control intact while ordinary RGB effects on HELIO's four-channel layer still reach the warm-white emitter.

## Rebase strategy

The current branch contains one local commit plus substantial uncommitted HELIO work, while upstream includes broad lifecycle and module-tree changes. Store the complete dirty state in a named recoverable stash, rebase the branch commit onto current `origin/next-iteration`, and apply the stash without dropping it. Resolve overlaps in favor of upstream, then reintroduce only HELIO behavior that has no upstream equivalent. Keep the stash until the rebased build and hardware checks pass.

## Verification

- USB Improv probe and provisioning complete without timeout.
- The setup AP remains available while a browser is connected.
- A dropped station reconnects without requiring a reset or repeated button presses.
- Opening or abandoning the UI cannot hold the render loop in repeated socket-write deadlines.
- Opening the UI does not allocate a frame-sized JSON block on the device; failed state responses stop after the first failed write.
- RMT frame memory scales as `lights * output channels`, not `lights * output channels * 8 symbols * 4 bytes`.
- A 222-light GRBW configuration transmits 888 data bytes and updates the complete physical strand.
- Existing HELIO state containing the accidental generic ripple replacement migrates to `HelioRippleEffect` at boot.
- `ctest --output-on-failure` passes.
- `uv run moondeck/scenario/run_scenario.py` passes.
- The `helio1` ESP32 image builds and flashes successfully.
- A scene switch produces the incoming frame immediately and leaves no pixels from the outgoing scene latched.
