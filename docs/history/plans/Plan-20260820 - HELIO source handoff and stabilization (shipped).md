# HELIO source handoff and stabilization

## Intent

Turn the locally verified HELIO worktree into one reviewable, reproducible source snapshot before migration to the Mac/OpenClaw environment. Preserve the full product context without committing generated caches, temporary captures, credentials, or device-local configuration.

## Scope

1. Inventory the branch, worktrees, remotes, related pull-request branches, changed files, hardware profiles, and observed live-device state.
2. Record durable operating knowledge in the project agent instructions by linking to one canonical operational handoff.
3. Add `CURRENT.md` for the active product goal and `docs/ops/MAC_OPENCLAW_MIGRATION_HANDOFF.md` for the exact source, test, recovery, and migration state.
4. Treat the current conflict-free working-file contents as the intended resolution of the index's stale unmerged entries after checking for conflict markers and reviewing the resulting diff.
5. Run every applicable commit gate for the combined source, firmware, UI, test, documentation, and device-catalog changes.
6. Create one combined commit, audit it for secrets, and push the HELIO branch to the public fork.
7. Run an independent whole-diff review, repair release-blocking findings, and record lower-priority limits explicitly rather than hiding them in an oversized snapshot.

## Exclusions

- `.zig-cache/`, `.zig-global-cache/`, `tmp/`, build trees, and other generated output
- Device credentials, browser state, Wi-Fi passwords, tokens, private keys, and local ESP-IDF or Python environments
- Rebasing, force-pushing, deleting worktrees, or changing the separate upstream PR branches

## Acceptance

- The handoff identifies what exists, what is verified, what remains physically unverified, and the next safe step.
- No working source file contains merge markers and Git records no unmerged path in the committed snapshot.
- Required desktop, unit, scenario, host-side, platform-boundary, ESP32, catalog, and KPI checks pass or the commit records an explicit, truthful exception.
- The pushed fork branch resolves to the same commit and tree as the local branch.

## Outcome

The stabilization review identifies three software paths that can corrupt physical RMT output while the preview source remains clean: cache-disabled interrupt starvation, reuse or teardown of a still-borrowed TX frame after a wait timeout, and control writes racing the active core-1 encode. The ESP32 configuration keeps the refill ISR cache-safe, the driver retains an immutable in-flight frame and channel until every transfer reports completion, and the scheduler reaches a frame boundary before writing worker-visible state. Platform worker stop is a true join, and diagnostic loopback timeouts retain a reclaimable handle/source pair.

The same review closes release-blocking issues in password serialization and diagnostic export, cross-core WebSocket access including release, OTA rollback confirmation, Pixelblaze execution/allocation bounds, and BH1750 scheduler blocking. Public passwords are set-only, downloaded diagnostics use an aggregate allowlist, WebSocket client state has one lock after worker quiescence, OTA validity follows successful module setup, the interpreter has one frame-wide instruction budget with top-level-only arrays, and sensor I2C lives on a worker.

The final ownership review also finds two teardown-only gaps: deleting an RMT driver after a bounded release timeout could discard the last handle/frame owner, and a loopback RX task could outlive stack state or a caller-freed DMA capture. The driver destructor drains its final lease, every RX loopback uses an atomic two-owner capture context, and each generic ESP worker owns a watchdog while its caller true-joins. Desktop lifetime tests and ESP source-contract tests pin those boundaries.

The final mutation audit finds one remaining cross-core gap outside the generic control setter: editable-list operations could resize a light-preset pool and rebuild driver corrections while core 1 encoded the previous frame, while direct WLED or MQTT preset application could overwrite a captured subtree through the same window. Every list add, move, field edit, and delete now reaches the render frame boundary before mutation, and `applyPreset` does the same immediately before its first subtree write. A host source-contract regression pins mutation and rebuild ordering across both entry paths.

The deployment audit reproduces a second watchdog boundary: direct OTA streams synchronously on the subscribed render task, so a slow routed upload can make healthy progress for longer than the watchdog interval and still reset. The OTA writer now feeds that task after each successful flash chunk; socket idle and whole-request deadlines continue to distinguish progress from a wedge. A source-contract regression and the ESP32 build pin the fix.

The complete desktop, scenario, host, catalog, platform, KPI, and `helio1` build gates pass. An independent final review reports no P0-P3 finding after the list/preset fix. The remaining multi-client resync acknowledgement, generic multi-driver preview correction, periodic filesystem accounting, trusted-LAN administration boundary, fault-injected worker/RMT recovery, and sustained physical no-flash observation stay explicit in the operational handoff.
