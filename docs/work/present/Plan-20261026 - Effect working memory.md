# Effect working memory

The owner's active task is the current MoonLight product integration, including
usable new animations and preserved BLE/web control. This bug-fix record uses
that standing scope; it does not authorize a customer release.

The layer already reduces its raster and maps the result to every physical
destination. Its admission check also needs the enabled effects' working set.
Add a cold-path footprint query to EffectBase, implemented for Color Trails
and Particles. Account for reusable owned scratch and the largest individual
allocation. Unlimited desktop/PSRAM cases retain their existing output.
Failed required scratch allocations release the incomplete set and expose a
status; an empty canvas holds no scratch. No brightness or physical-size cap
is introduced. This replaces the buffer-only admission decision for these
effects and removes unused partial allocations.

First pin partial-allocation cleanup/recovery and constrained-raster physical
coverage on the host. Run the focused regressions before/after, existing
effect goldens, full host tests and the relevant layer scenario. Only then
build immutable native artifacts and use the existing single-writer bench
handoff and standing flash authorization. At the device, require visible
output, changing source frames, BLE and cold web-UI coexistence; record the
actual reduced raster and optical limitations. Preserve and restore settings.

Per-effect hard limits would duplicate memory policy and miss combinations.
Only labeling the failure would leave the effects unusable. Shared predictive
admission reuses the existing adaptive raster without another render path.

Native application 7c1e1537 renders changing Color Trails source frames with
approximately 44 KiB free, but a cold page remains at its generic shell.
Loading control state leaves approximately 35 KiB, below the upstream 40 KiB
send floor. Restore the product send-budget policy over the current resumable
sender: shared 32 KiB reserve, 1 KiB chunks, generic defaults unchanged.
The existing send lease remains the single fairness mechanism. Two host
regressions fail at 35 KiB before this change: embedded assets and a complete
fragmented WebSocket state. Check the shared reserve remains protected.

The first bounded-send native check passes Color Trails cold UI/BLE and
154 changing full RGBW frames. Particles admits a full raster but leaves only
32,276 bytes idle, below the send reserve once another client opens. Products
now reserve the existing generic HTTP floor (40 KiB) for render admission,
while their resumable sender retains its bounded 32 KiB/1 KiB policy. Generic
layer admission stays at the shared allocator reserve. This separates render
admission from network queue admission and avoids per-effect size limits.
The layer status includes actual reduced dimensions for source provenance.
This per-layer policy is applied to the product's wired rendering layer;
future product-generated layers must carry the same policy.

The 5f09fbb8 native retry still admits a full Particles raster and leaves
32,440 bytes idle. The isolated host test passes because it prepares the
parent before the child. A new regression through applyStateDocument and
Scheduler::preparePending reproduces the API order: a new child already owns
11,416 scratch bytes, and the zero-growth fast path bypasses admission. Two
assertions fail before correction. Reclaim forecasted scratch and the old
layer buffer when a changed effect set cannot be admitted; the parent then
chooses a raster before the child rebuilds its working set. An unchanged
admitted scene retains its allocations under temporary pressure. Seven
focused tests / 51 assertions pass after this correction. Native proof is
still pending. The owner explicitly authorizes the targeted correction and
raises the local anti-stalling threshold to ten attempts.

Native f3ea858c passes both cases: 25×25×1 raster, all 222 RGBW positions,
39 source-preview frames each, 150/171 changing full RGBW frames, cold 390/1280px
UI and encrypted BLE coexistence. Original configuration is independently
compared after restoration. The complete host suite passes 2,882 cases and
183,461 assertions. No fresh optical acceptance or customer release is claimed.
