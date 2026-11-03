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

## Checkpoint, 2 November 2026: shared handset

Product boot now wires the shared handset as `Services.Remote` for HELIO and NEBULITE targets. Saved settings, disabled state and editable key bindings overlay that service. White commands and static-mode confirmation support RGB strips alongside a separate white output; native RGBW retains its dedicated white channel. See [handset behavior](../../reference/helio-remote.md).

The `nebulite-s3` desktop passes 14 handset cases (2,197 assertions), four BLE remote transport cases (68 assertions), and 219 driver cases (3,197 assertions). An isolated cold boot and filesystem restart retain a disabled Remote, a custom interval and a custom White binding, with exactly one Remote service. The pinned ESP-IDF 6.1 S3 build passes: application image 2,952,688 bytes, SHA-256 `ccb06ad19db6d1ddc41ddc766d5b7c7e1c0bbac4fa46fd643eaf9c045c5b6746`, with 193,040 bytes free in its application partition. Evidence and the bench candidate are preserved in `/home/veit/Documents/ChatGPT/HELIO/nebulite-handset-20261102/`.

No device was flashed for this checkpoint. Croptop and Jacket/Vest retain their source-derived profiles and need identified-board, handset-radio and physical-output acceptance. The classic ESP32 target remains experimental with no qualified product pin profile.

### Hardware-ID dependency

The shared firmware does not yet read the three ternary hardware-ID inputs; `System.deviceModel` remains configuration. The first export audit found identical fitted dividers; that historical snapshot is preserved in `hardware-id-audit.json`. Following the delivered PCB-owner handoff, the owner implemented distinct fitting codes in the saved native S/M/L originals on 2 November. The current native BOMs were independently rechecked:

| HELIO size | HW2/HW1/HW0 | Excluded resistors |
|---|---|---|
| S / 30 cm | 010 | R22, R26 |
| M / 50 cm | 020 | R22, R25, R26 |
| L / 70 cm | 100 | R22, R24 |

The PCB owner's native BOM and strict DRC proofs are under `/home/veit/git/helio-pcb/audit-work/hw-id-implementation-20261102/`. These fitting changes do not update existing physical boards, production exports, mixed panels or the JLC cart. Legacy NEBULITE code uniqueness remains open. The middle voltage requires qualified sampling before Wi-Fi for the documented GPIO14/15/16 ADC2 assignment; see Espressif's [S3 oneshot driver guide](https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/api-reference/peripherals/adc_oneshot.html). Firmware does not probe these pins or select outputs automatically.

### USB installation, 2 November 2026

With fresh owner authorization, the connected USB board was identified as ESP32-S3, 8 MB flash, MAC `DC:54:75:CC:82:00`. A complete 8 MB pre-flash backup was retained before installing `nebulite-s3` through MoonDeck's flash wrapper. Its normal native build refresh recompiled only `build_info.cpp`, updating the build timestamp and resulting binary identity; the tested application sources stayed unchanged. The installed 2,952,688-byte image has SHA-256 `55c08b6367e7907b63a18d263413ebd9ede760565fdb9e4af8fdc55b645fdf29` and is preserved as `nebulite-s3-usb-installed.bin`. The write completed with verified data hashes. The board rebooted successfully, answered Improv, and exposed RPC2 BLE identity with the same canonical MAC and firmware `nebulite-s3`. The serial log shows both BLE and Remote services running. This is bench firmware/runtime acceptance; physical handset reception and garment output remain unqualified for this installation.

The board is unprovisioned and starts access point `MM-8200`, with its local page at `http://4.3.2.1/` after joining that AP. The host remains on its existing Ethernet network; HTTP reachability from that network was not established. Current setup selects a compatible NEBULITE product profile within the installed target; it does not yet offer a cross-family HELIO/NEBULITE selector. `usb-acceptance.json` and the `usb-*` logs in the evidence directory record the installation and its limits.
