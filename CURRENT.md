# Current Project State

## Objective

HELIO 1 is a product firmware built on ProjectMM for an RGBW infinity-mirror sphere. The active work preserves the HELIO-specific product experience while minimizing divergence from current ProjectMM architecture: native effect modules render the selected scene, the HELIO tab orchestrates those modules, the physical driver emits stable GRBW data, and the browser previews the same device output without controlling the animation engine.

The source snapshot is prepared for migration to the existing Mac/OpenClaw installation. The complete operational and migration context lives in [`docs/ops/MAC_OPENCLAW_MIGRATION_HANDOFF.md`](docs/ops/MAC_OPENCLAW_MIGRATION_HANDOFF.md).

## Authoritative Source

- Workspace: `C:\Users\VEIT\git\projectMM`
- Branch: `codex/helio-private-wip`
- Push target: `fork/codex/helio-private-wip` on the public `Funkelfetisch/projectMM` fork; the branch contains no credentials or device-local configuration
- Upstream base: `MoonModules/projectMM` `main`
- Snapshot baseline: `e3aba94e47949867b3536f7a871af232d75490a0`; the branch tip containing this file is the migration revision
- Build variant: `helio1`

Resolve the exact commit and tree at transfer time with:

```powershell
git rev-parse HEAD
git rev-parse "HEAD^{tree}"
git status --short --branch
```

## Verified State

- The `helio1` ESP32 image builds successfully with ESP-IDF 6.1 beta tooling.
- Desktop unit tests and the desktop scenario suite pass against the current source contents.
- The pre-final stabilization image boots on the 222-light controller at `192.168.8.202` and the 311-light controller at `10.0.0.87`.
- The final source including list/preset quiescence and progress-fed direct OTA is build-verified. It is not installed on `10.0.0.87`: a clean `c5b9d94c` image reaches 720,738 of 1,828,544 bytes in 14.36 seconds before the older receiver resets, after which the controller returns on `e3aba94e+`.
- The 222-light profile uses a 50 cm ring, GRBW output, GPIO 35, accurate RGB-to-white extraction, and automatic ambient brightness.
- The 311-light profile uses a 70 cm ring, GRBW output, GPIO 35, and accurate RGB-to-white extraction.
- Preview-frame captures stay uniform during constant-color tests with multicore both enabled and disabled.
- Forced filesystem writes do not corrupt any captured source frame after the RMT cache-safe interrupt configuration is enabled.
- RMT source bytes remain immutable after a wait timeout until every channel reports completion; reconfigure, release, and final destruction retain or drain the borrowed frame and channel. Diagnostic RX tasks retain their DMA capture independently of caller timeout. Ordinary controls, editable-list writes, and direct preset application quiesce before touching worker-visible state, every generic ESP worker has watchdog-backed true-join teardown, and synchronous OTA feeds the render-task watchdog only after each successful flash write.
- BH1750 transactions run on a worker task, WebSocket client access is cross-core serialized, and hostile Pixelblaze loops share one bounded frame budget.
- Password controls are set-only over HTTP/WebSocket: public state contains only a configured boolean, while persistence retains the actual value locally. Downloaded diagnostics use a scalar/aggregate allowlist rather than exporting raw state.
- The commit gate records 107/107 module specs, 1/1 CTest target, 23/23 scenarios, 92 Python tests, 131 JavaScript tests, 29 valid board entries, and 10 valid firmware variants.

Physical observation remains the final authority. The cache-safe firmware needs a sustained visual run long enough to cover the formerly intermittent one-to-six flashes per minute.

## Next Work

1. Observe each physical strip for at least 20 minutes while ordinary control and persistence writes occur. Record any remaining flash with controller IP, active scene, brightness mode, and timestamp.
2. Rebase the private HELIO integration on current `origin/main`, resolve each overlap by preferring upstream primitives, and rerun the complete gate list plus both hardware profiles.
3. Rebase and reassess the two open draft PRs for BLE provisioning and automatic updates. Keep them draft until end-to-end hardware behavior is demonstrated on the current upstream base.
4. Run an end-to-end Improv BLE provisioning test. The required behavior is provisioning, not BLE OTA.
5. Compare every card simulation with the matching native C++ scene. The large and floating previews use live device pixels; cards remain JavaScript simulations and need scene-by-scene parity checks.
6. Design audio AGC/noise-floor tracking from measured envelope statistics rather than inferring gain from BPM.
7. Prefer native or statically compiled C++ ports for selected Pixelblaze patterns. The bounded interpreter is useful for compatibility but measures about 7 FPS for the observed use case.
8. Replace the global WebSocket resync acknowledgement with per-client generations before treating several simultaneous dashboards as a supported workflow.

## External Blocker

Mac/OpenClaw migration parameters remain unspecified: Mac SSH target, Mac project root, OpenClaw root, build root, Telegram forum/topic mapping, and ChatGPT account labels. Perform only read-only source preparation until those values are supplied together.
