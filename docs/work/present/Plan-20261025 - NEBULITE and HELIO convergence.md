# NEBULITE and HELIO convergence

Owner: existing firmware chat `01a0e402-48ae-7c30-827b-4326cf8bc4df`.

## Sources and preservation

MoonLight `f030e3f83ee74ddffd3e786884a29aec9ed4b517`, fetched from MoonModules/MoonLight on 25 October. The existing integration worktree contains the preserved HELIO port from `c57ff4bd`. The repair reference remains separate at `a649bd47` plus its uncommitted fixes. Verified source archives, tracked and staged diffs, manifests and the earlier all-refs bundle live outside git under `/home/veit/.local/state/nebulite-helio-firmware-backups/`.

The canonical product handoff is `/home/veit/Documents/ChatGPT/HELIO/NEBULITE-HELIO-FIRMWARE-HANDOFF-20261022.md`. Its app chat owns Android delivery. Firmware owns the protocol and hardware acceptance.

## Acceptance and order

1. Resolve the semantic port against current MoonLight, prefer its primitives, prove the desktop, then build HELIO without changing the running sphere during the website chat's measurements.
2. Carry the measured task-snapshot quiescence fix. Current upstream workers self-delete; the old external-delete/park fix is superseded. Ensure every RTOS join wait yields at least one real tick. Retest repeated sensor preparation and scene changes on hardware.
3. Serve the custom product dashboard at `/`, with the generic editor at `/modules`. Adapt state subscriptions and the separate pull-preview transport to current MoonLight. Preserve configuration and existing scene identities.
4. Reuse historical product wiring and BLE contracts through verified source inventories. Implement persistent BLE control independently of Wi-Fi association; publish explicit capabilities, state/error readback, reconnect and group timing semantics. App-only Wi-Fi OTA and combined USB images remain separate migration paths.
5. Qualify RGBW, representative RGB wearables, and mini-HELIO on actual identified hardware before customer rollout. The owner's 8 cm PCB/Fusion reference resolves to the current MINI80 S3/52-pixel design (data GPIO47, rail enable GPIO17), recorded in `docs/reference/helio-mini80-profile.json`. Its pixel order and maximum rail-settle time remain unqualified; do not activate it using the historical C3/16 or WLED 62-pixel profiles.

## Hardware evidence

The original sphere uses GPIO35, 222 RGBW pixels, an S3 with 8 MB flash and no PSRAM, and a 12 V strip supply. Native W and mixed RGB recovered on the same firmware after changing USB-PD port. The owner identified a 5 V-only port; the rail voltage was not measured. All-white emitter damage is contradicted by the recovered native W. The owner's present observation is no flashes. Capacitor charging remains a hypothesis.

The fixed-reference source sweep covers 73 effects, not 73 optical acceptances. Upstream integration and BLE are separate unqualified artifacts until built and bench-tested.

## Shared sphere and camera

The website chat released its controller and camera lease until explicit handback. Firmware accepted both on 25 October at 11:53:47 UTC in `helio-device-coordination.json`. Keep tablet view 2, ISO100, 16 ms exposure and daylight white balance unchanged. The private ADB server is 5053; the tablet capture endpoint is host port 18766. See [animation evaluation](../../reference/animation-evaluation.md) for the scoring method.

## Host acceptance checkpoint

The integrated desktop passes 2,825 C++ cases and 343 JavaScript tests; 17 desktop scenarios pass and 18 device-specific scenarios are skipped. The custom product dashboard opens at `/`; `/modules` opens current upstream's generic editor. Actual Chromium checks at 1280 and 390 pixels found no page errors or horizontal overflow; Controls fills the content width and the playlist cards use a responsive grid. Named fixture/palette readback is resolved rather than interpreted as a numeric row. These are host results, not ESP32 or optical acceptance. Evidence, protected configuration snapshots and rollback artifacts remain under the dated external backup directory.

ESP-IDF 6.1 is pinned to `fff9895c82d744c7237be8847347bdd1b07c6643`; its official container was independently checked before the native build. BLE host framing, state writes and scheduling tests pass. Linux and a nearby Android tablet paired with the identified sphere, issued encrypted commands and exercised Wi-Fi-independent control. A decoded Wi-Fi-return panic led to an mDNS teardown-order fix; three tablet-mediated transitions passed without a reset. Product radio memory and the on-device dashboard are being qualified. Multi-device physical group timing remains untested. See [the bench record](../../reference/product-bench-20261025.md) for evidence and boundaries.

## Current bench work

The complete USB retry over Tailscale verified both application slots. Current HELIO image `42d336b2` fixes the captured offline socket initialization panic and the independently reproduced task-snapshot animation freeze. Multicore output advances across repeated one-second snapshots. Camera cases confirm R, G, B, native warm white and mixed RGB. Three Wi-Fi/BLE cycles and a bonded BLE reconnection after a persisted Wi-Fi-off reboot pass; BLE restores LAN afterward, with no retained panic. The animation sweep is recording under the existing camera lease. Full-animation and multi-device acceptance remain separate.

A fresh upstream check found `abcdaec97adcb5664081d056628e6ea34309f8dc`, beyond the device-tested `f030e3f8` base. The complete dirty source was archived, hashed and read back before preparing the upstream BLE contribution in an isolated worktree. The original integration index and dirty work remain preserved. New upstream HTTP/send changes may supersede the product send-budget workaround; the semantic port must prove that before removing it from the new integration.

## Upstream publication authorization

Veit explicitly requested publication as a MoonModules/MoonLight pull request once BLE works on the actual controller. This authorizes the necessary scoped branch, commit, push and PR steps for the upstream contribution; existing unrelated dirty product work must remain preserved. Publication remains gated on real BLE acceptance, especially persisted Wi-Fi-off boot and reconnection. Follow the upstream contribution scope limits and disclose agent authorship and actual test boundaries. Do not include private configuration, local USB tools, credentials, product assets or unrelated HELIO/NEBULITE UI in a BLE-core contribution.

## Checkpoint, 27 October 2026

The common product tree is based on upstream `abcdaec97adcb5664081d056628e6ea34309f8dc`; a fresh remote main check on 26 October still returns that SHA. The scoped BLE contribution is PR158. Installed HELIO RPC2 image `a41701b9` passes native session and software-reboot checks. The existing app owner reports a real Native132 connection and catalogue discovery; Native133 first-pair tests retain the exclusive device lease. Firmware performs no device writes before explicit handback.

The next product preset/UI candidate passes the combined host suite and both native builds. It is preserved rather than installed during that lease. [Product presets and usage](Plan-20261026%20-%20product%20presets%20and%20usage.md) owns its exact acceptance and open telemetry work; [battery and groups](Plan-20261026%20-%20HELIO%20NEBULITE%20battery%20and%20groups.md) owns app, per-pack and physical group acceptance. Earlier checkpoints in this plan remain dated evidence, not the current installed identity.
