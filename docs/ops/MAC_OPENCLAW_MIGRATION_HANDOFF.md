# ProjectMM / HELIO Mac and OpenClaw Handoff

## Purpose

This document is the canonical operational handoff for the separate HELIO integration branch in ProjectMM. It gives a fresh agent enough evidence to build, flash, diagnose, and continue the work without relying on a Codex transcript or guessing from an old TODO. It also defines the exact source state that belongs in the Mac/OpenClaw migration.

Do not copy raw Codex sessions, browser profiles, local tool environments, build trees, or raw device-state captures. The repository plus this distilled handoff is the transferable context.

## Agent Startup Checklist

1. Read `AGENTS.md`, `CURRENT.md`, `docs/architecture.md`, the relevant module specs, and this handoff before changing source. Do not mine old plan files for context unless the product owner names one.
2. Work in the real ProjectMM checkout and inspect both staged and unstaged diffs. This branch carries one large coherent product snapshot; never discard an unfamiliar edit or treat the staging index as newer than the working file.
3. Confirm the target controller by allowlisted state facts such as device model, physical LED count, driver pin, channel order, active effect, and uptime. An IP address is a routing observation, not device identity; the two installed controllers have been confused during testing.
4. Never print, archive, or paste raw `/api/state`, browser storage, or device `.config` files. They can contain SSIDs, topology, calibration, and locally persisted secrets even though public password controls are set-only.
5. Separate the three observation layers: a JavaScript card is a deterministic illustration, the large/floating preview is the C++ frame before the peripheral, and the physical strip is the RMT waveform plus wiring and power. Agreement at one layer does not prove the layer after it.
6. Reproduce a physical-output fault with a simple constant RGBW frame before changing an effect. Record target identity, LED count, channel order, white mode, multicore state, ambient override, timestamp, and whether the live preview stays clean.
7. Build and test before flashing. OTA only a ProjectMM device with a compatible partition layout; use USB for an unproven WLED-to-ProjectMM first migration. Preserve NVS unless the product owner explicitly approves erasure.
8. After a flash, verify the booted build and allowlisted configuration, then observe the physical LEDs under Wi-Fi, browser preview, ordinary control changes, editable light-preset changes, scene application, and persistence writes. A successful upload is not a stability result.

## Product

HELIO 1 is an ESP32-S3 controller for a clear acrylic infinity-mirror sphere. One inward-facing RGBW strip follows the internal equator. The mirror repeats that strip as concentric rings; the LEDs are discrete direct sources, while reflected rings blur together more strongly. A nearby wall receives light transmitted through the sphere and a shadow from the rim.

The product experience starts in the HELIO tab. ProjectMM remains available beneath it, but HELIO branding hides the generic sidebar by default and provides an explicit way to reveal it. The HELIO logo selects the HELIO tab. Product controls, scene galleries, playlists, setup, automatic brightness, and preview optics live in that tab.

## Current Goal and Acceptance

The current goal is a stable, native ProjectMM HELIO firmware with these properties:

- One native effect module owns the active scene. A browser playlist selects and replaces modules; it does not run a hidden firmware autoplayer and does not block manual scene selection.
- Audio scenes use the real `AudioService` frame. Synthetic rhythmic input is opt-in through `idleBeat`, never an invisible default.
- A scene change has neither stale pixels nor a deliberate black pulse. The incoming scene renders in the same tick in which the old persistent buffer is cleared.
- GRBW strips receive real four-channel data. Accurate mode extracts the common RGB component into W, and an explicit W channel remains usable by effects such as Solid.
- The main and floating sphere previews consume the true RGB preview frame emitted by C++. Scene cards use bounded JavaScript simulations only while visible.
- HTTP/UI activity, persistence, Wi-Fi, and preview traffic do not interrupt the physical RMT stream.
- The 50 cm product profile defaults to 222 LEDs. A 70 cm installation uses 311 LEDs through persisted configuration.

Acceptance requires the complete automated gate set, successful `helio1` build, boot and state verification, and a physical observation period. A clean network preview proves source data; it cannot prove the electrical waveform after the ESP32 RMT peripheral.

## Source Inventory

| Item | State |
|---|---|
| Source workspace | `C:\Users\VEIT\git\projectMM` |
| Branch | `codex/helio-private-wip` |
| Baseline commit | `e3aba94e47949867b3536f7a871af232d75490a0` (`WIP: preserve HELIO ProjectMM fork work`) |
| Authoritative revision | Tip of `fork/codex/helio-private-wip` containing this handoff |
| Upstream | `origin` = `https://github.com/MoonModules/projectMM.git` |
| Private push target | `fork` = `https://github.com/Funkelfetisch/projectMM.git` |
| Upstream divergence before this snapshot | One local baseline commit ahead and 19 upstream commits behind `origin/main` |
| Submodules | None; no `.gitmodules` |
| Git LFS objects | None reported by `git lfs ls-files` |
| Generated exclusions | `/build/`, `/.zig-cache/`, `/.zig-global-cache/`, `/tmp/`, UI embedding output, SDK configs, logs, caches |

The final commit resolves the index's stale `UU` entries by staging the already conflict-free working-file contents. A successful firmware build and desktop tests use those same contents. No source file contains a merge marker.

### Related Worktrees and PRs

The following linked worktrees are clean and remain separate from the HELIO snapshot:

| Workspace | Branch | GitHub state |
|---|---|---|
| `C:\Users\VEIT\git\projectMM-pr-auto-update` | `codex/upstream-auto-update-manifest` | PR [#47](https://github.com/MoonModules/projectMM/pull/47), open draft, CodeRabbit success, no review decision |
| `C:\Users\VEIT\git\projectMM-pr-ble` | `codex/universal-ble-provisioning` | PR [#46](https://github.com/MoonModules/projectMM/pull/46), open draft, CodeRabbit success, no review decision |
| `C:\Users\VEIT\git\projectMM-pr-performance` | `codex/performance-frame-pacing` | PR [#45](https://github.com/MoonModules/projectMM/pull/45), closed |

PR [#48](https://github.com/MoonModules/projectMM/pull/48) for STA reconnect and PR [#49](https://github.com/MoonModules/projectMM/pull/49) for RGBW/RMT performance are closed. Their branch ideas overlap this HELIO integration and current upstream. Do not merge or cherry-pick them blindly. Rebase each surviving PR independently, compare with current upstream behavior, delete obsolete overlap, and preserve only the still-missing capability.

## Hardware Profiles

### HELIO 1 common wiring

| Capability | Configuration |
|---|---|
| MCU | ESP32-S3 |
| Firmware variant | `helio1` |
| LED data | GPIO 35 |
| LED channel order | GRBW |
| White conversion | Accurate |
| Physical white | Warm white, approximately 3000 K |
| Preview white point | RGB `(255, 176, 110)`; preview-only, not an output correction |
| Microphone SCK/BCLK | GPIO 40 |
| Microphone WS | GPIO 38 |
| Microphone data | GPIO 39 |
| Microphone MCLK | disabled (`-1`) |
| Audio sample rate selector | `2` = 22050 Hz |
| Audio floor / gain defaults | `75` / `1` |
| Ambient sensor | BH1750FVI-TR at I2C address `0x23` |
| Ambient I2C bus | bus 1, SCL GPIO 11, SDA GPIO 26 |

GPIO 26 may be reported by the generic pin inspector as reserved on some ESP32-S3 definitions. It is the intentional HELIO board connection and works on the tested hardware. Treat a new board revision separately rather than changing this product default from a generic warning alone.

### Installed controllers

| Address | Physical size | LED count | Notes |
|---|---:|---:|---|
| `192.168.8.202` | 50 cm | 222 | Current primary bench controller; BH1750 and local audio are present |
| `10.0.0.87` | 70 cm | 311 | Original home controller; configuration is migrated to current GRBW/Accurate enum values |

These addresses are LAN/VPN observations, not identity. Re-discover by mDNS, DHCP lease, serial output, or device list after a network change. Never bake either address into firmware.

The ambient calibration is device-local persistence. A recent 50 cm state reports approximately `darkLux=10`, `darkBrightness=50`, `brightLux=180`, `brightBrightness=255`, and `hysteresisLux=20`; source defaults remain `0`, `16`, `500`, `255`, and `15`. Preserve the persisted calibration unless the physical installation is recalibrated.

## Implemented Architecture

### Product boot wiring

`main.cpp` identifies the `helio1` product and code-wires:

- Device name/model defaults `HELIO` / `HELIO 1`.
- One `RingLayout` named `HELIO Circle`, defaulting to 222 physical lights.
- One four-channel `Layer` and a default `HelioRippleEffect` named `HELIO Ripple`.
- One `RmtLedDriver` on GPIO 35 with GRBW, Accurate white mode, and a 3000 K preview white point.
- One non-editable `PreviewDriver` that streams real light positions and RGB output.
- Local `AudioService` with the board microphone pins and silent real-input behavior when no audio is present.
- `Bh1750Service` on I2C bus 1.
- Manifest-driven `AutoUpdateModule` for the HELIO update endpoint.

Code-wired modules survive persistence overlays and old configuration trees. Persisted user calibration and size still override their defaults.

OTA rollback is enabled in the bootloader configuration. A newly booted image remains pending until `scheduler.setup()`, HELIO scene migration, and the required prepare sweep complete; only then does `markOtaAppValid()` cancel rollback. A fault before that point leaves the previous app eligible for bootloader recovery.

### Native HELIO scenes

The original saved WLED bundle is represented by dedicated ProjectMM modules rather than one opaque autoplay effect. The current scene catalog contains:

- Static family: Candle Glow, Color Sparkles, Amber Bands, Lake, Orbit, Pink Glow, Phase Shimmer, Ripple, and Chase.
- Audio family: Starburst, Gravity Center, Juggling Dots, Comets, Level Meter, Plasmoid, Wide Spectrum, Equalizer, Gravity Bands, and Frequency Wave.
- Native sphere additions: Orbiting Lobes, Harmonic Halo, Liquid Mirror, and Twin Suns.

Ripple uses the native `RingsEffect` in the UI catalog. The sphere additions reuse native ProjectMM effects. The other saved WLED scenes use dedicated `Helio*Effect` subclasses that preserve source defaults, oscillator behavior, palette names, trails, gravity, and audio gates.

The legacy `HelioWledPresetsEffect` remains a compatibility surface, not the HELIO dashboard's scene runner. A bounded `PixelblazeCompatEffect` parses a useful Pixelblaze subset, but interpreted per-pixel evaluation is too slow for this product's normal animation path. Use it for compatibility experiments; port selected patterns to native/static C++ after checking each source license.

### Scene orchestration

The HELIO UI maintains one scene slot. Selecting a card:

1. Stops a browser playlist unless the playlist itself initiates the change.
2. Queues a different click made while a replacement is already in flight.
3. Replaces the slot's module type through the REST endpoint.
4. Splices the returned module resource into browser state instead of waiting for a full-state round trip.
5. Applies only controls that differ, disables any other enabled effects, and enables the target once.

This prevents a second click from toggling the selected effect off and revealing a stale background. It also prevents multiple inactive scene modules from accumulating state.

The UI exposes three playlists:

- `Static Animations`: saved static sequence, 10 seconds per scene.
- `Audio Animations`: saved audio sequence, 30 seconds per scene; audio setup is checked first.
- `Sphere Optics`: native effects selected for the infinity-sphere geometry, 18 seconds per scene.

Playlist execution belongs to the browser session. Closing the controlling page stops its timer but leaves the currently selected native scene running on the controller.

### Layer persistence and scene switches

Many LED effects require the previous frame for trails, so `Layer` normally preserves its buffer. Two guards are essential:

- With no enabled effect child, clear the layer and reset the pending fade. Otherwise the physical LEDs retain the last frame indefinitely.
- When the enabled effect set changes, clear once and render the incoming effect immediately in the same tick. This removes inherited trails without inserting an intentional black frame.

Physical-order HELIO effects use an identity buffer over the real strip count. They do not allocate and remap the sparse bounding box of the geometric ring.

### RGBW behavior

HELIO strips are GRBW, not GRB. Different products may use other orders, so channel order remains a driver preset rather than a global assumption.

The layer carries four logical channels. `Correction` applies brightness first, then maps logical channels to physical roles:

- An explicit source W value is preserved.
- If W is zero and white synthesis is enabled, `Min` computes `min(R,G,B)` for W while retaining RGB.
- `Accurate` computes the same W and subtracts it from each RGB channel, so neutral light moves to the efficient physical white emitter.
- `None` disables synthesized W but still passes an explicit source W channel.

The preview converts physical W back to the warm preview white point. This affects only browser appearance. It does not tint or reduce the real white channel.

### Automatic brightness

`Bh1750Service` keeps the BH1750 in continuous high-resolution mode and publishes a transient 0..255 target. The blocking device-open, command-write, and read operations execute on a worker task; the scheduler tick only consumes an atomic result. The persisted manual Drivers brightness remains untouched and becomes the immediate fallback when automatic control is disabled or the sensor is invalid.

Two lux/brightness anchors define a linear curve. The anchors may be reversed. `hysteresisLux` accepts a new measurement only when it differs enough from the last accepted value, preventing visible brightness jitter. Updating any anchor recomputes the target immediately. The HELIO UI disables the manual brightness slider while a valid automatic target overrides it and explains that the slider is the fallback.

### Preview system

There are two deliberately different preview sources:

- Large HELIO preview and floating window: strict device source. `PreviewDriver` sends actual physical light positions and corrected display RGB; the renderer reports `waiting` instead of inventing rainbow data when no fresh frame exists.
- Scene cards: deterministic JavaScript simulations. They render at normal cadence only inside the viewport, in small batches, and pause during scene replacement or while the floating preview owns rendering time.

The card simulator receives each scene's speed, intensity, palette, RGBW, custom controls, LED count, and deterministic synthetic preview-audio envelope. It is a catalog illustration, not proof of native C++ parity. Lake, Chase, Juggling Dots, and Amber Bands have required repeated parity work and remain important regression checks.

`HelioTrace` is a WebGL analytical optics model with a canvas fallback. Important current defaults and controls include:

- Six large and five card reflections, capped at 25.
- Discrete direct LEDs with configurable fill and gain.
- Off LEDs visible at 30% geometry brightness below threshold 12, but excluded entirely from reflections and wall emission.
- Clear glass with a small thickness response, dichroic tint, radial glow, reflection attenuation/cutoff, and no random dither source.
- Optional wall, wall distance, moving strip-derived wall projection, 110-degree emission concept, inverse-distance falloff, and rim shadow.
- Static, slow tilt, and 45-degree swivel camera modes. Card previews auto-tilt; the main preview does not rotate unless its mode is enabled.
- Separate large/card DPR, backing-size cap, FPS, and card batch controls stored in browser local storage.

The renderer must not add free-floating dots: every visible reflection derives from a lit LED sample and follows a reflection line. Wall illumination also derives from strip position; a radial average-color gradient is not an acceptable substitute.

### UI and transport stability

The HELIO page uses a two-column scene gallery under each playlist, a folded setup block at the bottom, color pickers in place of exposed RGBW sliders, a vertical action rail beside the main preview, and an explicit floating-preview open/close path. The HELIO favicon and first-paint branding load before generic ProjectMM content becomes visible.

The full module tree is large because effects expose palettes and metadata. Serializing and pushing it every second competes with rendering and network buffers. The current HTTP/WebSocket design therefore:

- Streams full state through `JsonSink` without one large contiguous allocation.
- Sends a small resync notice after structural change; the browser fetches the full state and establishes a new leaf-hash baseline.
- Sends value patches on ordinary one-second updates.
- Returns the replacement module resource from the replace endpoint.
- Retries listener binding after failure with a one-second backoff and rebinds when the live port changes.
- Guards client-table reads, upgrades, closes, text sends, and resumable preview cursors with the same non-blocking cross-core sender lock. Module release first joins/quiesces the encode worker and then yields until it owns that lease before cancelling a borrowed preview frame or closing clients; a transient busy lock cannot skip cleanup.
- Keeps the preview driver's experimental resumable transport disabled by default because a shared send slot can splice a preempted binary frame. The synchronous path skips rather than blocks when the socket is busy.

These choices reduce browser stalls and protect the LED render loop. A UI page that becomes slower over several seconds after a scene switch usually indicates state/preview transport contention, not an expensive option form alone.

## Failure Knowledge

### Random physical flashes

Observed symptom: one to six times per minute, arbitrary LEDs show arbitrary colors for roughly 100-200 ms while the intended animation resumes immediately.

Evidence separates the renderer from the peripheral:

- Three minutes with Drivers multicore enabled: 2,426 captured preview frames, no malformed frame, no non-uniform constant-red frame, and one unique source hash.
- Three minutes with multicore disabled: 3,656 captured frames with the same clean result.
- The source payload lives in internal RAM and a 222-pixel RGBW RMT frame lasts roughly 9 ms.

ESP-IDF documents that an RMT TX interrupt is deferred by default while the flash cache is disabled. Long transmissions use ping-pong refill; a filesystem or NVS write in that window can therefore produce unpredictable peripheral output even though the source buffer remains perfect.

Four independent firmware invariants protect this path:

1. `CONFIG_RMT_TX_ISR_CACHE_SAFE=y` in `esp32/sdkconfig.defaults` keeps the RMT refill ISR, encoder functions, and required driver state available while flash cache is disabled. The project uses the analogous IRAM/GDMA protection for Parlio.
2. The byte encoder borrows `RmtLedDriver::frame_` until `rmt_tx_wait_all_done` succeeds. The previous void wait discarded a timeout and the next render tick rewrote that same buffer while DMA could still read it, producing a mixed wire frame with arbitrary colors. The wait now returns completion, each channel retains an `inFlight` lease after timeout, and later ticks poll with zero timeout while leaving the frame immutable.
3. Reconfiguration, disable, release, and the optional loopback tests obey the same ownership contract. Teardown waits before `rmt_disable`; a timeout leaves the channel and its compact source frame intact. Normal output retries through its driver lifecycle, and object destruction is the final drain boundary: it cannot discard a still-live channel or borrowed frame. Diagnostic loopback TX moves into a fixed table sized to the SoC's TX-channel count, and every later RMT initialization reaps completed entries. Diagnostic RX uses one atomic, intrusive two-owner context shared by caller and FreeRTOS task; a late receive timeout keeps its DMA capture alive until the task publishes completion and releases its reference. These bounded ownership paths may retain a small allocation until transfer completion or reboot, but they cannot lose a handle or feed RMT freed memory.
4. Every render-visible mutation reaches the Drivers frame boundary before its first write. The scheduler does this before the generic parser touches an ordinary control or enabled flag. Editable-list handlers do it before add, move, field edit, or delete and only then rebuild dependent controls and driver corrections; this matters because changing a light preset can resize its backing pool while a driver reads the old correction. `ControlModule::applyPreset` does it immediately before `applySubtree`, which also covers direct WLED and MQTT scene application. Brightness, geometry, scene, light-preset, and loopback changes therefore cannot race the core-1 encode. A healthy worker remains alive after the boundary. `stopPinnedTask()` is a true join for release and timeout degradation: it yields until the worker callback returns, then deletes the suspended RTOS task and only then permits owner memory or the HTTP broadcaster to be released. Every generic ESP worker owns a task-watchdog subscription, so the joining caller can wait safely while a genuinely wedged callback triggers recovery on its own task.

The editable-list/preset race can explain a flash correlated with a scene or preset action. It does not execute while the device is truly idle, so an idle flash after this image is running remains evidence for the peripheral/electrical path rather than a reason to reopen scene orchestration first.

A desktop HAL seam deliberately holds a transfer in flight. Its regression tests force `release` to retain the borrowed frame and force object deletion to wait until another thread publishes completion. Scheduler and render-split regressions separately prove quiesce-before-control-write ordering and that a successful frame-boundary wait preserves the active worker. Host source contracts prove the frame-boundary hook precedes every editable-list mutation, dependent correction rebuild, direct preset subtree write, and every retained ESP loopback task's release/acquire publication. These tests pin ownership and ordering without pretending to emulate the ESP32 waveform.

A forced-write stress test toggles and persists `Preview.resumableFrames` twelve times while capturing constant red. It records 1,225 preview frames with no malformed, non-uniform, or wrong-color frame and no reboot. That capture predates the explicit frame-lease and teardown corrections and proves the C++ source stayed clean, not that the physical RMT stream did. The physical strip still needs the sustained human observation noted in `CURRENT.md` after the final image containing all protections is flashed.

Attribution is mixed rather than "all upstream" or "all HELIO": the cache-disabled RMT refill behavior is an ESP-IDF/platform configuration fact that ordinary short strips and quiet networks may not expose; the missing borrowed-frame lifecycle was a firmware integration bug made important by the byte-encoder path; and power, level shifting, grounding, cable geometry, or a damaged strip can produce the same visible symptom after both software protections are present. A clean preview narrows the fault to peripheral/electrical output, while a clean 20-minute physical soak under Wi-Fi and persistence load is the closure criterion.

### Wrong colors and the end of the strip

A 50 cm controller configured for 311 LEDs while only 222 exist sends a frame longer than the installation and makes end-of-strip diagnosis misleading. Always set both `HELIO Circle.nrOfLEDs` and `RmtLed.ledsPerPin` to the physical count.

Channel-order symptoms are diagnostic:

- Requested red appearing as blue, green appearing as white, or blue appearing as red points to a preset/order mismatch or a corrupt serialized frame.
- Uniform warm-white flashing on an otherwise stable RGB color points to corruption of the fourth channel, not an RGB palette problem.
- Solid white failing while RGB effects work points to explicit W being discarded; the four-channel logical buffer and `Correction::logicalRgbw` fix this class.

Do not change GRBW to GRB merely because one observed color looks yellow-green. Correct extraction moves the common RGB component into W; channel order still follows the actual strip.

### Stuck pixels after effect changes

The old failure sequence disables the only effect, leaves the persistent layer buffer latched, then enables or overlays another module. Clicking the same scene alternates between the valid animation and the stale buffer. The `Layer` no-effect clear and active-effect signature guard remove that hidden background.

### Double black flash on scene change

Separate disable, replace, parameter, and enable requests can expose intermediate black frames. The current orchestration replaces one slot, returns the fresh resource immediately, applies only needed controls, and enables once. `Layer` clears the old persistent frame and renders the new effect in the same tick.

### Wi-Fi credential truncation and AP loss

Text inputs are drafts. Typing `StudioNetVPN` must not attempt a connection after the intermediate text `Studio`; only `connectWifi` applies credentials. `NetworkModule` copies edited values through temporary buffers before reusing its own backing storage, marks persistence dirty, and starts the STA state machine. An empty SSID returns to AP mode.

The required provisioning test covers repeated failed STA attempts, AP availability throughout entry, explicit connect, saved full SSID, reboot reconnect, and fallback after the target network disappears. Site-to-site VPN reachability proves LAN routing, not local AP robustness.

### Intermittent HTTP loss and UI lag

The HTTP listener retries after a failed bind, but only once per second. Full-state updates use streamed REST plus small WebSocket patches. Preview sending and every client-table mutation use one non-blocking cross-core lease, with bounded would-block behavior so a slow browser cannot stall the encoder task. Direct firmware upload remains synchronous and therefore pauses rendering, but it has separate idle and whole-request deadlines and resets the render-task watchdog after each successfully written 4 KB chunk. A slow but progressing LAN upload therefore survives; a stalled socket still aborts.

### Scheduler stalls from optional modules

`tick1s()` still runs on the render scheduler and therefore cannot perform a nominally "slow" blocking transaction. The initial BH1750 implementation did up to one I2C open, two command writes, or a 20 ms read there; a timeout could be visible as an LED hitch. Its I2C ownership now lives in a worker and uses a single-request/single-result atomic handshake. A desktop regression injects 200 ms I2C latency and verifies the scheduler-facing call returns in under 50 ms.

The Pixelblaze compatibility interpreter also needs a whole-frame bound. A per-loop 4,096-iteration guard alone permits nested loops to multiply to millions of operations per pixel. One shared instruction budget now covers global setup, controls, `beforeRender`, all pixels, nested calls, statements, and expressions. Exhaustion clears the effect rather than leaving the previous frame latched. Pixelblaze arrays use the platform allocator, report their capacity as dynamic memory, and may allocate only during top-level program initialization. An `array()` or `makePIController()` assignment reached from `beforeRender`, `render`, or a nested frame-time function invalidates the program and clears output; allocation failure during compile is likewise explicit instead of silently producing a partially valid program.

## Build, Test, and Flash

### Windows source environment

- ESP-IDF root: `C:\Users\VEIT\esp\esp-idf`
- User-installed uv: `C:\Users\VEIT\AppData\Roaming\Python\Python312\Scripts\uv.exe`
- Workspace-local uv cache: `C:\Users\VEIT\git\projectMM\build\uv-cache`
- Verified desktop test tree: `build/windows-zig-verified`
- ESP-IDF CTest executable used when `ctest` is absent from PATH: `C:\Users\VEIT\.espressif\tools\cmake\4.0.3\bin\ctest.exe`

Use uv for every Python invocation:

```powershell
$env:UV_CACHE_DIR='C:\Users\VEIT\git\projectMM\build\uv-cache'
$env:PYTHONUTF8='1'
$uv='C:\Users\VEIT\AppData\Roaming\Python\Python312\Scripts\uv.exe'
& $uv run moondeck/build/build_esp32.py --firmware helio1
```

The UTF-8 setting keeps KPI output portable when Windows otherwise selects CP1252. Repository scripts that ask uv for an auxiliary Python package invoke `python` inside that uv environment; `python3` is not a Windows command and may resolve to the Microsoft Store alias instead of the managed interpreter.

The verified clean deployment-attempt image is `build/esp32-helio1/projectMM.bin`, source label `c5b9d94c`, size `0x1be6c0` / 1,828,544 bytes, with 42% of the app partition free. ESP-IDF reports 1,828,419 bytes of linked image data before binary padding. Its SHA-256 is `778361B8A9952D647EECC4181D9C3520A41A6FC47346D62E13A7BF1DD6BC3E17`. A build from a later documentation-only branch tip embeds that commit identity and therefore has a different binary hash despite identical firmware behavior.

The controller at `10.0.0.87` reports source label `e3aba94e+`; a reboot alone is not evidence that an OTA committed. The final measured direct attempt transfers 720,738 of 1,828,544 bytes in 14.36 seconds at about 50 KB/s before the old receiver resets the connection, and the controller then returns on the same build. Earlier direct attempts fail at the same receiver-watchdog boundary. Worker-based URL OTA cannot connect back through the site-to-site/firewall route to a tightly scoped workstation server, and no ESP32 USB serial port is present. Do not erase NVS or publish the firmware just to bridge this deployment gap: use USB, a faster direct route, or a private HTTP(S) origin reachable by the controller. Once the current image is installed, its progress-fed direct upload path supports the same slow routed transfer for later updates.

Run the required checks from the repository root:

```powershell
& $uv run moondeck/check/check_specs.py
& 'C:\Users\VEIT\.espressif\tools\cmake\4.0.3\bin\cmake.exe' --build build/windows-zig-verified
& 'C:\Users\VEIT\.espressif\tools\cmake\4.0.3\bin\ctest.exe' --test-dir build/windows-zig-verified --output-on-failure
& $uv run moondeck/scenario/run_scenario.py
& $uv run moondeck/check/check_platform_boundary.py
& $uv run moondeck/build/build_esp32.py --firmware helio1
& $uv run moondeck/check/check_devices.py
& $uv run moondeck/check/check_firmwares.py
& $uv run --with pytest --with pyserial --with markdown --with wled pytest test/python -q
node --test "test/js/**/*.test.mjs"
& $uv run moondeck/check/collect_kpi.py --commit
```

Scenario runs intentionally update `observed.desktop-windows` blocks in scenario JSON files. The KPI command provisions `lizard` through uv and updates the tracked repository-health snapshot.

### KPI interpretation

The KPI one-liner is the useful performance record: it contains one maximum tick per scenario in runner order. The single desktop number in `docs/metrics/repo-health.md` is currently only the first element of that series. The first runnable scenario is `scenario_MoonModule_control_change`, a 4,096-light Noise + Multiply + NetworkSend pipeline whose repeated maxima in this stabilization run vary around 297-420 microseconds; the previous repository snapshot happened to record 127 microseconds. That headline is host scheduling noise around one scenario, not a controlled before/after benchmark and not evidence that the ESP32 slowed by a stable factor. The live ESP32 KPI remains 2,151 microseconds / 464 FPS.

Use the complete per-scenario series and named `scenario_perf_light` / `scenario_perf_full` steps for performance decisions. A future metrics cleanup should key the repository headline to a named benchmark instead of array position; do not optimize production output merely to move this unstable aggregate.

### Network upload

Use HTTP/1.0, disable `Expect: 100-continue`, and avoid an artificially slow body:

```powershell
curl.exe -sS --http1.0 --connect-timeout 10 --max-time 60 --limit-rate 512k `
  -X POST http://10.0.0.87/api/firmware/upload `
  -H "Content-Type: application/octet-stream" `
  -H "Expect:" `
  --data-binary "@build/esp32-helio1/projectMM.bin"
```

Replace only the host for the 50 cm controller. Success returns `{"ok":true}` and the controller reboots. Verify the build, boot reason, ring count, RMT count, preset, white mode, active effect, ambient status, and uptime after reboot. Do not print or archive the full response.

An initial controller still running WLED is not proven compatible with ProjectMM's `/api/firmware/upload` route. Use USB for the first ProjectMM flash unless a WLED-native OTA path is explicitly bench-tested with the exact partition layout. Subsequent ProjectMM images use the route above.

### USB recovery

Re-probe the COM port immediately before flashing; ports change after resets and between boards. Use the project MoonDeck flash script rather than raw esptool so the selected firmware and partition offsets stay consistent:

```powershell
& $uv run moondeck/build/flash_esp32.py --firmware helio1 --port COMx
```

Do not erase NVS or the full flash during ordinary recovery. Erasure destroys calibration and credentials and requires explicit product-owner approval.

## Verification Record

| Check | Result |
|---|---|
| `helio1` ESP32 build with RMT cache-safe config | Pass |
| Module spec/source alignment | Pass, 107/107 modules |
| ESP32 image boot on 222-light controller | Pass |
| ESP32 image boot on 311-light controller | Pass |
| Desktop CTest in `build/windows-zig-verified` | Pass, 1/1 top-level test |
| Scenario runner | Pass, 23/23 scenarios |
| Python host contracts | Pass, 92 tests |
| JavaScript host/UI contracts | Pass, 130 tests |
| Platform boundary | Pass |
| Device catalog | Pass, 29 boards |
| Firmware projection | Pass, 10 variants |
| Commit KPI collection | Pass; 16,384-light desktop scenarios recorded and ESP32 image remains 42% free |
| Constant-red preview capture, multicore on | Pass, 2,426 clean frames over three minutes |
| Constant-red preview capture, multicore off | Pass, 3,656 clean frames over three minutes |
| Twelve persisted writes plus parallel preview capture | Pass, 1,225 clean frames, no reboot |
| RMT timeout source-lifetime regression | Pass by desktop emulation: release retains a deliberately borrowed frame after timeout; destruction waits for completion before freeing owner memory |
| RMT diagnostic task lifetime | Pass by ESP source contract and `helio1` compile: one retained atomic context owns RX state and DMA capture across caller timeout |
| Cross-core render-visible mutation | Pass: scheduler hooks precede ordinary/enabled writes, list hooks precede add/move/edit/delete and correction rebuild, direct preset hooks precede subtree writes, and a successful render quiesce preserves the active worker |
| Worker teardown ownership | Pass by desktop join regression, ESP source contract, direct implementation review, and `helio1` compile; each generic ESP worker owns a watchdog while the caller true-joins |
| Direct OTA watchdog | Pass by host source contract and `helio1` compile; each successful flash chunk feeds the subscribed render task, while socket idle and whole-request deadlines remain bounded |
| Final image on `10.0.0.87` | Blocked: the old receiver resets after 720,738 of 1,828,544 bytes at about 50 KB/s; URL OTA cannot connect back to the workstation and no ESP32 USB port is present |
| BH1750 scheduler isolation | Pass with 200 ms injected desktop I2C latency |
| Pixelblaze execution safety | Pass; one frame-wide budget terminates the nested 4,096 x 4,096 loop fixture, runtime arrays are rejected, forced array OOM is explicit, and failed output clears |
| Public password serialization | Pass; stored mode round-trips the value, public mode emits only `true` or `false` |
| WebSocket cross-core client locking | Pass by host source-contract test and desktop build, including quiesce-before-lock release ordering |
| Independent stabilization review | Pass with no P0-P3 finding after the final list/preset mutation fix; hardware soak remains outside static review |
| Downloaded diagnostics privacy | Pass by host contract: only scalar health and aggregate counts leave the device; raw state, module names, controls, statuses, origin, and network configuration are excluded |
| OTA rollback confirmation after module setup | Pass by host source-contract test; hardware rollback fault injection not performed |
| 222-light restored runtime state | Pass: 222/222, GRBW, active HELIO scene, multicore on |
| 311-light restored runtime state | Pass: 311/311, GRBW, Accurate, default Ripple disabled |
| Sustained physical no-flash observation after cache-safe build | Pending product-owner observation |
| BLE Improv provisioning end to end | Not verified |
| Manifest auto-update from a published release | Not verified |
| Every card preview versus physical scene | Not verified scene by scene |
| Pixelblaze static-compilation path | Not implemented |

## Security and Secrets

Required secret names are:

- Wi-Fi SSID and Wi-Fi password for each installation
- GitHub authentication for the public fork and PR operations; audit every pushed file because branch contents are publicly readable
- Mac SSH authentication
- ChatGPT sign-in contexts for primary and optional reserve Codex accounts
- Existing OpenClaw and Telegram credentials on the Mac

No value belongs in this repository or migration archive. In particular:

- Password controls in `/api/state`, compact state, and WebSocket patches contain only a configured boolean. The browser field is blank and set-only; an untouched Wi-Fi password draft preserves the device value.
- Device `.config` files retain actual Wi-Fi and MQTT passwords for reboot persistence. The unauthenticated File Manager is an administrator surface on the trusted LAN and can expose those files; do not fetch, archive, or migrate them casually.
- Raw state still includes SSIDs, IP configuration, module topology, and installation calibration. Diagnostics extract an allowlist even though password text is redacted.
- Do not copy browser local storage, cookies, Codex auth files, SSH private keys, `.env` files, ESP-IDF credentials, or Wi-Fi configuration JSON.
- Do not copy `build/`, Zig caches, uv caches, `tmp/`, monitor logs, generated embedded UI, or SDK-generated `sdkconfig` files.

## Open Questions and Known Limits

1. **Physical flash closure:** automated evidence strongly identifies flash-cache interruption of RMT refill, but only the product owner's eyes close the intermittent electrical symptom after the final image is actually installed.
2. **Upstream convergence:** the HELIO branch is intentionally not rebased in this snapshot. Current upstream is 19 commits ahead of the baseline and already contains overlapping RGBW, driver, networking, or UI primitives.
3. **BLE provisioning:** the target is Improv BLE provisioning. BLE OTA is a different capability and is not requested.
4. **Audio adaptation:** fixed floor/gain works only for one environment. A robust design tracks a slow noise-floor percentile, a faster signal envelope, bounded AGC, attack/release, and beat confidence with a refractory interval. A 120 BPM track suggests two beats per second, but BPM alone cannot define microphone gain or guarantee visible peaks.
5. **Preview parity:** the large preview is authoritative for incoming network/LED data because it displays device frames. Card simulations need direct scene fixtures or captured reference traces to become exact.
6. **Pixelblaze:** interpretation trades flexibility for speed. For a fixed commercial scene set, native/static C++ effects are the appropriate hot path. Preserve source provenance and license review when selecting PatternDB inputs.
7. **OTA bootstrap:** a controller running an older build can still reset while receiving a large direct upload over a slow routed link because that receiver lacks the progress-based watchdog feed. The current image fixes future direct uploads, but installing it on such a controller requires USB, a faster direct path, or a private HTTP(S) origin that the controller can reach for worker-based URL OTA.
8. **Multi-client resync:** the state-patch baseline is global. Any successful full-state fetch clears the global pending flag, so one unrelated REST client can theoretically acknowledge a resync before every WebSocket browser has fetched it. HELIO's tested workflow uses one dashboard; a supported multi-dashboard workflow needs per-client resync generations or explicit acknowledgements.
9. **Multi-driver preview correction:** preview color correction currently resolves the first output driver. That is correct for the one-driver HELIO product profile but is ambiguous for a generic installation with several differently corrected outputs.
10. **Periodic filesystem accounting:** File Manager refreshes filesystem usage every five minutes on `tick1s`. It is outside the observed one-to-six-per-minute flash pattern, but moving that query to a worker is the remaining strict hot-path cleanup.
11. **Trusted-LAN API boundary:** HTTP remains unauthenticated and deliberately CORS-accessible for browser installers and local tooling. Set-only state removes password disclosure, but the File Manager and control endpoints remain administrator capabilities; do not expose the controller directly to an untrusted network.

## Mac/OpenClaw Migration

### Inputs still required

Do not invent these values:

- `SOURCE_MACHINE_LABEL`
- `MAC_SSH_TARGET`
- `MAC_PROJECT_ROOT`
- `MAC_OPENCLAW_ROOT`
- `MAC_BUILD_ROOT`
- `TELEGRAM_FORUM` and the project topic identifier
- `PRIMARY_CHATGPT_ACCOUNT_LABEL`
- Optional `RESERVE_CHATGPT_ACCOUNT_LABEL`

### Transfer set

Preferred transfer is the pushed Git branch. The fork is public, so verify that the branch contains no credentials or device-local configuration. Transfer all Git objects needed by `codex/helio-private-wip` and preserve the three related worktrees as separate branches/checkouts only when they remain active. Transfer no build output or machine-local state.

At source and destination record:

```sh
git rev-parse HEAD
git rev-parse 'HEAD^{tree}'
git status --short --branch
git worktree list --porcelain
git lfs ls-files
```

Hash `AGENTS.md`, `CURRENT.md`, and this handoff on both machines. Source and destination commit/tree IDs and document hashes must match. A dirty destination is not accepted as a migrated snapshot.

### Mac dependencies

- Git and Git LFS command support, even though this snapshot has no LFS object
- CMake and a C++20 desktop toolchain
- uv
- Node.js for host-side JavaScript tests
- ESP-IDF version compatible with the repository's build tooling
- USB serial tooling for local hardware recovery
- Official Codex CLI authenticated through `Sign in with ChatGPT`, with no API-key fallback
- Existing OpenClaw installation and its existing Telegram bot/forum integration

### OpenClaw project agent

Create exactly one ProjectMM/HELIO agent whose workspace is the real Mac checkout. Its persistent bootstrap states:

- Identity: ProjectMM/HELIO firmware and product-integration agent.
- Workspace: exact Mac checkout path.
- Current goal: the objective and acceptance criteria in this handoff.
- Loaded instructions: `AGENTS.md`, `CURRENT.md`, `docs/architecture.md`, `docs/coding-standards.md`, relevant module specs, and this handoff.
- Next work: physical flash closure, upstream rebase, BLE provisioning test, preview parity, and per-client WebSocket resync.
- Blocked actions: deployment, release, force push, secret transfer, account switching, scheduler activation, and destructive flash erase without explicit approval.
- Git state: exact migrated branch, commit, and tree.

Bind one Telegram forum topic to this agent and workspace. A simple CWD binding is insufficient; the native OpenClaw project-agent configuration must hold the identity, workspace, and persistent context.

The topic smoke test asks the agent for its project, exact workspace, current goal, loaded agent documents, next three TODOs, and blocked actions. Every answer must match this handoff. Keep all scheduler jobs disabled.

## Recommended Next Safe Step

1. Complete the physical no-flash observation on the newly flashed controller.
2. Supply the missing Mac/OpenClaw parameters in one message.
3. Clone or fetch the HELIO branch on the Mac and verify commit, tree, clean status, and document hashes.
4. Build and run desktop/host tests locally on the Mac before configuring OpenClaw.
5. Initialize one isolated OpenClaw project agent and Telegram topic, then run the six-question context smoke test.
6. Rebase and upstream only after the migrated HELIO snapshot is reproducible and recoverable.
