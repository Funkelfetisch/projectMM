# Product presets and usage

Veit requested persistent per-animation controls, editable/deletable cards, new presets on the same page, faster interaction, and optional usage/preset sharing.

An isolated NEBULITE-S3 desktop instance was given a new Sine preset with
speed 41 and intensity 73, applied through the actual conditional preset API.
After the debounced Effects configuration was observed on disk, a normal
process stop and restart using the same private storage restored the enabled
Sine effect and both values; the preset file retained the same values too.
This confirms this software restart path only. It is not a power-loss test or
a verification on the physical sphere or wearable.

The latest verified bench core image is `58876fad`, with catalogue `877e4018bfbb`; the UI-only read-backoff candidate is built and its installation is pending. Native133 completes the targeted re-pair and actual mixed-device group power checks; Native134 independently passes bonded warm reconnect with 93 correlated responses and all 24 scenes. The [BLE repair plan](Plan-20261027%20-%20BLE%20re-pair%20recovery.md) owns that evidence. RPC2 factory-scene semantics stay defaults plus overrides.

## Source checkpoint, 26 October

The native preset store, conditional HTTP resources, shared factory document writer and per-card editor are implemented in the working tree. The combined tree passes 2,990 C++ cases (195,564 assertions), 413 JavaScript cases, three Python catalogue cases and 144 module specifications. Local browser checks pass 390/1280 px: independent accordions, saved settings and palette names after reload, CRUD, conflicts, guarded playback and one automatic reload for the old UI. These checks use a local fixture rather than the device.

Both native builds pass at that checkpoint. The immutable HELIO candidate is `d5799ee69df815e3138109d19c8460a4cb5ac7a93cad93f0a856c0f328da6c5f`, 2,671,184 bytes, runtime timestamp `Oct 26 2026 22:03:41`. The NEBULITE candidate is `62093d5d032361f35be43fcfb2fdeebe905d2a2612819ad16bf2f09ff350a88b`, 2,908,432 bytes. Those candidates were preserved rather than installed. Their catalogue is `1f31fab289c4`; the then-installed image advertises `f381ef4a29da`. Fresh catalogue discovery is required after installation.

The earlier Rings rate correction carries fractional elapsed-time growth but still draws only whole-pixel radii. At speed 1 this changes the image approximately 7.8 times per second despite a measured 48 render frames per second. The current source draws the radius and band falloff in 1/128-pixel units, preserving the rate and whole-pixel respawn boundary; age fade is computed once per ring. A shared exact 32-bit distance-root path avoids software 64-bit division for ordinary grids while preserving the wide range. Host and native qualification are separate. Other frame-coupled WLED ports and the owner's unidentified fast Sphere Optics scene still need device and visual qualification; no general speed cap is introduced.

Help generation completes, but the full documentation gate reports 130 errors and 2,053 warnings, including comment formatting and missing product module help/assets. Release acceptance remains open. Device API, memory, UI, startup and optical checks wait for explicit App handback; elapsed time does not reclaim the device.

Local usage accumulation is implemented as a pure, opt-in bounded session counter; its seven unit cases pass. It records actual scene/settings hashes, monotonic runtime, UTC days when known, brightness bins and explicit overflow/unknown-clock fields. Firmware integration, reboot persistence, a visible consent/export flow and the real upload destination remain open. The existing app analytics code excludes MoonLight transports and its activation is held in the app's CURRENT.md. It supplies no verified preset-sharing receiver. The app owner has been asked for an explicit receiver/auth/consent contract. No telemetry collection or upload is activated.

Acceptance must preserve existing presets and running settings, verify native conditional-file and output guards, test startup, and distinguish logical readback from optical observations. The actual app pilot and real battery calibration remain separate gates.

## Bounded index checkpoint, 27 October

The index now streams one preset's metadata from a bounded name snapshot through the existing send pump. File and parser storage end before that fragment is queued. Wire keys and conditional mutation behavior remain unchanged. Startup failure is answered before HTTP 200; a later read failure closes an incomplete document, which the browser rejects without deleting retained records or drafts. The shared memory reserve remains intact.

The combined source passes 3,019 C++ cases with 198,380 assertions and 428 JavaScript cases. Ten focused AddressSanitizer cases pass with 2,068 assertions, including 64-entry streaming with large platform allocations refused, early/late failures and cleanup. The allocator seam proves file/fragment failures; parser allocation uses a different allocator and is not claimed as tested. Independent source review finds no blocker in the index start, refill, EOF or cleanup paths.

Both native index builds pass for `helio1` and `nebulite-s3`; the separate device checks remain open. The Website chat holds the explicitly released static material-measurement window from 02:37:54 UTC; firmware makes no controller or camera accesses before its fresh handback. The earlier prototype setup/manual-white/lifetime candidate is also archived with both native builds and remains uninstalled. [Product HTTP memory and latency](../future/Product%20HTTP%20memory%20and%20latency.md) owns the actual slow-stream observations and remaining acceptance.


## Selection and short-response checkpoint, 27 October

The source releases a confirmed native selection before status observers finish. Saved per-card controls remain immediately usable; runtime controls show a pending state until a targeted Effects resource read succeeds. Superseded reads and waiting legacy selections cannot replace the latest native selection. Content-only saves advance file revisions without forcing schema replacement; short prepared HTTP replies progress within the existing floor and budgets beside a tree lease.

The combined source passes 3,024 C++ cases with 198,800 assertions in the existing GCC Debug HELIO matrix and 447 JavaScript cases. The isolated browser fixture passes 390/1280 px, including a single cache reload, retained edits and conflicts, no horizontal overflow and selection without a full state GET. Independent source review has no remaining finding in this selection/notification/scheduling scope. An incidental default GCC Release build exposes separate allocation-check diagnostics, tracked in [Desktop Release allocation checks](../future/Desktop%20Release%20allocation%20checks.md); no Release pass is claimed.

The Website explicitly returned the controller at 03:16:19 UTC. Firmware accepted the exclusive lease and confirmed the existing MAC, image, running 222-light output and unchanged retained panic with read-only requests. Both current native builds and the guarded OTA, index, short-reply, configuration-preservation and BLE checks follow these host gates; their acceptance remains separate. No new optical or battery calibration is established.


The next fragment-fairness source passes 3,031 C++ cases with 200,300 assertions and both native builds. Seven new cases cover constrained JSON allocation, early pressure refusal, exact native preset bytes and cleanup, metadata interleaving and concurrent preview/HTTP delivery. The earlier patch-OOM fixture now refuses the actual 256-byte initial allocation; its complete-resync expectation is unchanged. The 447-case JavaScript and 390/1280 px fixture evidence is inherited only after exact UI source hashes match. Native latency and file-preservation acceptance still follow; the installed `29dedb88` source remains archived with its failed metadata probe.


The transactional retry core passes 3,035 C++ cases and 23 focused AddressSanitizer cases. Its native read-backoff workload passes all 24 templates and conditional preset file operations, including overlapping complete metadata and short GET/PUT responses. No lighting mutation occurs; original preset ETags and running documents match afterward. The following UI-only source passes 454 JavaScript cases and the 390/1280 px browser fixture. It adds bounded GET-only 503 retries and a background-preview cooldown. C/C++ and memory-test source inheritance is checked by 662 exact file hashes; native UI acceptance is separate.


The owner confirms visibly smoother Rings on installed image `297ed8d4`. One observed idle sample falls from17.7ms effect time and46 renderFPS to8.6ms and89 renderFPS; these counters describe renderer work rather than an optical frame-rate measurement. The current source adds `background brightness`0–255/default0 to Rings and the shared scene catalog. Existing cards hydrate only missing descriptors/defaults when Controls opens, retaining user values, paths, origin, slot, ETag and drafts. Hydration neither schedules a save nor plays a scene. The next real edit uses the existing conditional save. The procedural Rings preview includes the same additive palette background; native optical equivalence remains separate.

The Website thread owns the current static W1/controller-camera window. Firmware source and host validation continue offline; any next device operation waits for a fresh explicit scoped restoration and handback.
