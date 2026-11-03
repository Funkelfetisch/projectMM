# Product HTTP memory and latency

The latest identified HELIO bench observation runs image `83c5ddd4` with UI `20261027k`. Preset file operations and complete metadata work in the bounded native workload, with original files and configuration preserved. Cold dashboard loading remains slow and can end with an incomplete preset index while multiple browser connections and preview are active. The following observations record the earlier candidates and the current source correction; they do not establish release readiness.

The parallel identity probe uses `/api/modules/Firmware`, the same asynchronous tree sender as the large metadata response. Below 51,200 bytes of internal heap, the current sender holds its lease for the entire response. Matching probe latency therefore does not establish a blocked scheduler. The next device measurement pairs a large metadata request with `/api/modules/Firmware/document` or `/api/system` and records request start, header, final body byte and EOF separately. Perform it only after an explicit App device handback, without lighting writes.

That distinguishing measurement runs after the explicit App134 handback at 02:02:20 UTC. Effects metadata takes 3.46 seconds while its independent Firmware document probe completes in 69.6 ms. Drivers metadata takes 11.85 seconds while `/api/system` completes in 150.1 ms. This confirms that independent request handling continues during the slow stream. The exact large-stream bottleneck remains open. All three configuration documents and the retained panic match before and after; no device writes occur. Evidence is `http-latency-20261027/run.json` in the dated external firmware backup capsule.

After idle, internal free heap recovers from the immediate post-workload 32,968 bytes to 39,680 bytes, above the 38,616-byte baseline. The observed drop therefore does not establish a leak. Fragment allocations and unacknowledged TCP buffers sharing the narrow margin above the reserve remain a hypothesis to distinguish from link/ACK latency.

The preset index retains its complete JSON body while admitting the send. Fragmenting index entries through the existing sender can reduce that retained memory; the shared 32 KiB reserve remains intact. Entry read failures, concurrent preset edits and disconnect cleanup must have explicit bounded behavior. Finite host memory tests precede device reads; a successful build does not establish latency or optical acceptance.

The working-tree implementation adds a `PresetIndex` source to the existing send pump. A shared store helper serializes one entry; a bounded snapshot holds at most 64 names of 32 bytes. File contents and parser storage are freed before each fragment is queued. The JSON keys remain `items`, `limit` and `active`. The snapshot and first entry are prepared before sending 200; a later deletion, read error or allocation failure closes the incomplete document instead of returning a valid prefix. The current browser client waits for complete JSON before replacing cards. Membership is snapshotted, individual content tags are read later, and no atomic whole-index revision is claimed. Parser allocation failure remains an error, and unusually large metadata does not acquire an arbitrary new entry-size limit. Host qualification passes 3,019 C++ cases, 428 JavaScript cases and ten focused AddressSanitizer cases; actual native index acceptance remains open after the Website measurement handback.

The current source removes the ineffective `/api/state?compact=1` query. Confirmed card selection, master power and palette changes no longer wait for a full metadata read. Runtime Effects controls request their actual named module resource lazily, coalesce reads and reject superseded results; Audio setup also reads its actual module. Rare module-add and older-server replacement fallbacks retain the full read.

Content-only preset saves retain the current metadata schema. The Control row comparison still announces additions, deletions and visible metadata changes. Prepared HTTP answers with at most 4 KiB remaining can progress beside a low-memory tree lease. This is a scheduling policy, not an existing hard response-size cap. Actual write room, the shared memory reserve, byte budget, time budget and stall window still apply; short answers do not acquire or extend the tree lease. New source and host checks precede a native build and device latency acceptance.


## Native selection-candidate result and fragment correction

Candidate `29dedb88` installs successfully and preserves the exact Drivers, Effects and Services documents. Its fresh backup contains all 25 preset-folder files; the post-update file comparison remains open. It has no new panic. Its first native smoke check stops before any QA write: System metadata takes 6.07 seconds, then Firmware metadata returns HTTP 200 with an empty body after 10.07 seconds. Independent health and identity complete in 75/95 ms. The new index returns all 24 entries, but takes 12.73 seconds; a 1,276-byte preset read takes 2.05 seconds. This is an open acceptance failure rather than an application or hardware success claim.

The no-PSRAM build uses a 2,048-byte initial JsonSink allocation for even short fragments. At a measured 34,712 bytes free, only 1,944 bytes remain above the send reserve. An owned fragment can therefore consume its own admission margin and stay unsent until the stall timeout. The exact sample does not identify a particular active slot, but the failure path is present in the source. Whole-response leases and Preview preceding the rotating senders add separate head-of-line and scheduling risks.

The next source starts JSON buffers at 256 bytes and retains geometric growth. Preset GET transfers its existing file buffer instead of copying it into another rounded JSON buffer. HTTP metadata prepares its first fragment before returning 200; inadequate send headroom returns 503 and frees the slot. Later unsendable fragments close the incomplete stream promptly. Completed generated fragments free their owned buffer before yielding the low-memory lease. Preview, control and HTTP slots share the rotating start, the existing byte budget and the shared time budget. Existing buffered replies may wait through external pressure; reserved replies to already committed mutations retain their outcome semantics. Host and native acceptance follow independently.


## Transactional retry result, 27 October

Image `58876fad` retains each cursor until its trial fragment is admitted. Temporary pressure frees that trial and yields without advancing or closing the established stream. Fragment targets use existing ceilings and a share of the remaining send room. The combined source passes 3,035 C++ cases and 23 focused AddressSanitizer cases.

On the bench, complete Effects metadata takes 2.84 seconds, Drivers 1.85 seconds and Services 0.61 seconds. The preset index returns all 24 entries in 2.79 seconds. A rapid factory-template sweep encounters a transient 503; the same isolated resource then returns in 95 ms. The repeat sweep with two bounded, GET-only retries passes all 24 templates, native file creation/read/conditional update/stale-tag refusal, forbidden hardware and name checks, and cleanup. Exactly one read retries after 150 ms. No write is retried. Original preset ETags and all three running configuration documents remain unchanged.

During an overlapping 6.73-second Drivers stream, the QA preset GET completes in 95 ms and its conditional PUT in 316 ms. Both finish before the complete large response. The write stores only the QA file and leaves lighting unchanged. Evidence is `http-pressure-retry-20261027/native-presets-read-backoff` in the external firmware capsule. These are individual workload observations, not guaranteed latency bounds.

The subsequent UI-only candidate adds the same bounded 503 read retry and a two-second background-preview cooldown. All 454 JavaScript cases and isolated 390/1280 px browser checks pass; 662 unchanged C/C++ and test source hashes retain the earlier host/memory qualification. Native installation, protected BLE readback and actual served-dashboard checks follow separately.


## Actual dashboard bootstrap, 27 October

Image `2c0689bb` serves the exact bundled UI `20261027c`. Existing Linux-bond protected reads and fresh RPC2 return all 24 scenes. The live dashboard has no horizontal overflow at 390/1280 px, but cold card readiness takes 30–35 seconds and a second read-only visit exceeds 45 seconds. This remains a failed usability gate.

The recorded waterfall shows the initial WebSocket subscription and, after 1.2 seconds without a complete frame, an additional full `/api/state` request. Both transfer approximately 90 KiB. Both can render the page and independently reload the preset index. The generic interface uses the WebSocket alone. UI `20261027d` removes the timer and its lifecycle state; explicit resync/compact events retain their schema request. Three regressions cover a slow fragmented snapshot, reconnect before completion, and an initial compact event. All 457 JavaScript cases pass.

A read-only browser asset override tests this source against the unchanged installed image before any new flash. The duplicate REST request disappears, but a preset-index response still closes after 1,018 bytes and approximately 11 seconds. Blocking only the test browser's preview socket yields all 24 cards after 22.47 seconds; its controls still take 2.7–3.1 seconds. Other owner-owned Brave connections remain active and are preserved. These observations establish remaining resource contention, not a guaranteed single-client latency or a proven leak. Native build and acceptance of this next source remain pending.

Evidence: `http-read-backoff-20261027/dashboard-network-baseline` and `http-bootstrap-20261027/browser-override{,-no-preview}` in the external firmware capsule.

The next source extends the existing background-stream guard to active HTTP Tree, Types and PresetIndex sources while the existing sender policy reports low headroom. The three background callers pause before advancing their walk, leaf hashes, pending changes or resync flags. They resume after the response ends. File transfers, already prepared mutation receipts, control input and sender draining remain independent. Continuous metadata traffic can postpone background status; previously held patch buffers are retained. This prevents new competing patch production during the named response and does not claim a general queue-admission or fairness solution. Host and native qualification are pending.

The corrected desktop source passes all 3,038 C++ cases and 201,754 assertions. The first run exposed only an overstrong new test expectation about file-transfer send leases; its original failed log is retained. The corrected tests check background queue admission separately from wire scheduling, retain the high-headroom concurrent case, and verify the exact pending value after HTTP EOF. UI source passes all 457 JavaScript cases and the 390/1280 px fixture. Sanitizer, native builds and fresh device acceptance remain separate gates.

The focused AddressSanitizer gate passes 29 cases and 3,991 assertions, including all three new background-transfer cases. The exact filter and leak-detection-disabled environment are retained with the log. Native HELIO and NEBULITE S3 builds follow serially; no new device installation is claimed by these host results.


## Identified native background-guard result

Both native builds pass. The HELIO bench installs `2b96674d` and identifies UI `20261027d`; the actual served minified JavaScript matches the built bundle byte for byte. Existing Linux-bond protected reads and fresh correlated RPC2 return all 24 scenes without pairing or repair. Drivers, Effects and Services documents, the four output flags and the historical panic record match the pre-update baseline.

Generic file reads during the owner's active Brave connections still produce short responses: ten of 25 reads contain zero bytes or a prefix. The subsequent isolated read after the owner closes the HELIO tab returns all 25 files byte-identical to the fresh pre-update backup, without a retry. This distinguishes transfer truncation from lost stored presets; the concurrent generic-file path remains an open issue.

The actual isolated dashboard returns all 24 cards after 14.54 seconds, compared with earlier 30–35-second observations. Its two tested controls open in 418 and 384 ms, with no horizontal overflow at 390/1280 px and no page errors or lighting writes. No duplicate `/api/state` request occurs. This is an improvement, not a satisfactory cold-start bound or a multiple-client guarantee. Evidence is `http-background-pressure-20261027/{dashboard-native,preserved-native-files-isolated.json,final-settings-checkpoint.json}` in the external capsule.


## Rings background native checkpoint, 27 October

Image `4c4de331` serves UI `20261027e` and catalog `10f1185373e7`. Existing Linux-bond protected reads and fresh correlated RPC2 return all 24 scenes. All 25 original preset-folder files match the pre-update backup before the deliberate Ripple edit. The actual dashboard then saves only Ripple through its conditional file update, retaining its existing values and adding background brightness 16. A separate scoped persistent state patch sets the active Rings background to 16 without replacing the scene; speed 1, count 4, thickness 5 and hue shift 0 remain. Drivers, Services, output flags and the historical panic record remain unchanged.

The actual cold dashboard takes 14.38 seconds to show its cards; cold-start usability and concurrent generic-file truncation remain open. The new background control has state and persistence acceptance, with no new optical calibration or all-animation acceptance claim. Evidence is `rings-background-20261027/{actual-background-ui/report.json,background-activation/report.json,final-checkpoint.json}` in the external firmware capsule. At 13:47:39 UTC, controller and T811 ownership passes explicitly to Website for bounded static geometry/exposure measurements; firmware waits for an explicit handback.


## Values-first dashboard and immediate controls

The next source requests the existing WebSocket state with `{ "subscribe": ["*"], "detail": "values" }`. It retains canonical control values, types, readonly/hidden flags and module outlines while omitting rendering metadata, options and list details. The default subscription retains the full representation. Fragment retries, resync, patches and reconnect retain their existing framing and scheduling. A values snapshot echoes `detail:"values"`; the dashboard loads full module metadata only for an explicitly opened view. Late metadata reads merge intervening acknowledged or pushed values.

Preset Controls render the saved schema immediately; additive template fields arrive independently. A focused input remains attached until focus leaves the control content. Loading and schema hydration preserve drafts, metadata, values and conditional revisions without writes. The source also adds the Rings `max pause ms` control, range0–2000/default500; user-rejected ground light is already removed from current Rings and saved Ripple, with speed1 preserved. Host, offline browser and identified native acceptance remain separate gates.


## Values-first native result and remaining pacing

HELIO installs image `94b970e3`, UI `20261027f`, catalog `bfe5e37ed69b`. All25 preset-folder files remain byte-identical to the fresh backup. Current Drivers and Services remain unchanged; Effects adds only the new Rings default pause500, retaining background0 and speed1. Existing Linux-bond protected reads and correlated RPC2 return24 scenes and the native pause descriptor:uint16,0–2000/default500. The historical panic record remains unchanged.

The actual served dashboard produces a28.1kB values snapshot after about5.2 seconds from navigation. Its24 cards are ready in7.33/7.82 seconds at390/1280px; cold Controls open in450/1140ms. Both layouts have no horizontal overflow or page errors, and the check blocks all lighting writes. These functional checks pass; cold usability remains open. Source and offline browser timings alone do not establish the native latency target.

The traced low-headroom drain path releases each generated fragment and returns immediately even when no other sender waits and the client still has part of its1024-byte quantum. At a256-byte fragment target, this can underuse each20ms turn. The next source retains the existing byte/time budgets and consults the existing waiter ordering after release before giving up the turn. Additional initial assets and reserved foreground preset-read capacity are examined separately. No follow-up native acceptance is claimed. Evidence:`ui-latency-rings-cadence-20261027/{native-checkpoint.json,native-readonly/run.json,native-ui-1793118543490772072/report.json}` in the external firmware capsule.


The follow-up source reuses the remaining send quantum only after freeing the previous owned fragment and consulting the existing waiter order. A drained or expired turn takes no new lease. The existing1024-byte per-client quantum, shared3ms time budget and heap floor remain unchanged. Loopback under forced low headroom sends1024 bytes for an isolated sender; a waiting peer receives its1024-byte turn after the old140-byte fragment remainder. Complete JSON, WebSocket frame ordering and pressure paths pass the133-case HTTP subset,3055-case C++ suite and48 focused ASAN cases. Native transfer timing remains open.

UI20261027g defers the MoonModules PNG and Prism to the generic interface; HELIO retains its own branding and the generic fallback retains its editor. One of the existing two read slots remains available to a primary preset-file request. Background previews, template schemas and palette metadata share the other slot; a promoted file read is coalesced. Small template reads queue before palette metadata. All485 JavaScript cases pass. Device builds and fresh native cold-load/Controls measurements follow the browser gate.


## Deferred renderer and NamesOnly native observations

UIi moves the unchanged HelioTrace renderer behind the settled preset-card paint,
with a shared import, honest loading/retry and independent controls. The actual
embedded entry falls from78315 to67464 gzip bytes; the renderer is a separate
11775-byte asset. Local browser checks exercise a pending import, actual failed
ESM load followed by explicit retry, unchanged canvas/focus and saved-customer
preset behavior. Native cold cards still take5.78/6.12s at390/1280px; controls take
348/333ms. This leaves the cold-start issue open.

Image4422e7a3/UIj opts only the preset-name snapshot into the generic NamesOnly
filesystem mode. The pinned LittleFS readdir supplies file/directory types, so
known entries avoid the redundant per-entry stat. Unknown types retain the prior
classification fallback; Metadata remains the default for file-manager sizes.
Fresh gates pass3057 C++ cases/223221 assertions,43 focused ASAN cases/2766
assertions with leak detection disabled,511 JS cases and5 tests compiling the
actual ESP filesystem source with POSIX recorders. Both native profiles build;
all25 preset-folder files and Drivers/Effects/Services remain exact, with the
historical panic unchanged and protected existing-bond RPC2 reads complete.

On the identified device, the isolated24-entry index starts after91.85ms and
finishes after1118.85ms, compared with480.76/1532.42ms on UIi. The simultaneous
67465-byte app finishes in2754.63ms instead of3135.49ms. Cold cards take5.21/5.30s,
with controls932/232ms; these individual observations are not a latency bound.

A read-only values-snapshot TCP_INFO observation records28107 JSON bytes,28215
TCP data bytes and79 received data segments over1625ms. The existing tree sender
writes each WebSocket fragment header and JSON body separately. Their packet and
queue overhead is a hypothesis to test, not an established explanation of the
entire delay. The seven-root subscription projection saves about210ms in
isolation, but expanding it while a generic metadata GET completes can rebuild
an editor and lose unsent input or focus. That client change is not implemented.
Evidence resides in the external capsules `deferred-helio-renderer-20261027` and
`names-only-preset-index-20261027`, including native-checkpoint, timing, socket
metadata and immutable source/build manifests.


## Early saved controls and coalesced WebSocket frames

UIk mounts the real native preset collection after verified firmware identity and
the canonical preset index, while the first complete live state is still pending.
It retains one client, collection, connected wrapper and preset host. The initial
stage reserves its normal layout and reports the pending connection; runtime
power, brightness and geometry appear only with the real state. First-state
hydration fills the reserved stage/panels around the same preset host. It keeps
focused inputs, drafts and in-flight conditional saves. Palette/schema hydration
uses the existing generation and focus guard. NEBULITE and the generic module
editor retain their existing startup. No fake state or additional index request
is introduced.

The WebSocket Tree sender reserves ten bytes in its existing owned fragment and
places the normal frame header immediately before the JSON. The allocation base
remains the owner; header and payload share the existing bounded write span.
HTTP, pressure rollback, FIN/Pong boundaries, the1024-byte per-client quantum and
shared3ms time budget retain their behavior. Tiny fragment targets still advance.
The independent source reviews find no blocker. Fresh checks pass3059 C++ cases,
18 focused ASAN cases with leak detection disabled,527 JavaScript cases and four
real local browser cases. The local withheld-state test retains a focused draft
through its conditional save and first-state/metadata hydration; viewport shifts
are0 or0.27 pixels across390/1280px and initially open/closed global brightness.

Both native profiles build from the1891-file immutable archive8510e956. Only the
identified HELIO receives the single reviewed OTA. Native cold cards take3.63 and
3.74 seconds, versus the post40 UIj observations5.47 and5.73 seconds. Controls open
in240 and836ms and the cards precede their live values snapshot by about1.4 seconds.
Three immediate real scene clicks remain enabled and coalesce to the finalRipple
apply. All25 preset-folder files and Drivers/Effects/Services remain exact; the
historical panic record is unchanged. Existing-bond protected RPC2 reads return
all24 scenes and complete beside an actual WebSocket values transfer. No pairing
repair, camera acquisition, optical qualification or customer rollout occurs.

An isolated values transfer receives53 data segments in1.24 seconds; the earlier
post40 UIj sample receives78 in1.41 seconds. These are finite observations, not
bounds or an attribution of every latency change to header coalescing. The native
mapping allocation also changes back to identity fallback after the OTA/reprepare,
so equal saved controls do not establish equal runtime mapping. Cold startup
still includes the67,881-byte entry transfer; the11,775-byte renderer stays
separate and waits for real runtime data. Evidence resides in the external
`ws-tree-coalescing-20261027` capsule, including `native-checkpoint.json`.

29 October: three cold mobile-browser loads against an isolated desktop `nebulite-s3` source build took 12.43–12.47 s to the home/cards, dominated by uncompressed on-disk `helio-app.js` (8.32 s) and `style.css` (2.07 s) under the product's existing 1024-byte-per-20ms quantum. This desktop development path serves raw files, not embedded gzip, so it is not a device timing. Separately, single sequential read-only GETs against the installed .202 image returned gzip `helio-app.js` (67,881 bytes) in 1.72 s, `style.css` (9,441 bytes) in 0.27 s, `helio-style.css` (4,944 bytes) in 0.20 s and bootstrap (731 bytes) in 0.09 s. These individual transfers do not measure full cold page readiness or interaction latency; no browser load or device writes were performed. A larger send quantum has not been accepted against the existing shared heap floor, other clients or LED cadence.

29 October, scoped real-controller browser measurement after explicit firmware ownership: a new isolated 390px desktop-Chromium context reached DOMContentLoaded in 423 ms and the HELIO home in 3,243 ms; 24 preset cards were present at measurement. Slowest requests were compressed `helio-app.js` 2,093 ms and `/api/presets` 1,555 ms. Document width equalled viewport width; there were no browser page errors. A separate cold session opened the first `Amber Bands` controls and rendered five inputs 238 ms after click. Browser routing blocked all non-GET/HEAD HTTP requests; none were attempted. System MAC was verified, lamp on=false/brightness=252 before and after. This single desktop-browser sample, with the lamp off, does not prove physical-animation cadence, phone performance or preserved full configuration, and it is the installed older UI, not the new source candidate.

On 29 October, a single read-only request to the currently running .202 returned HTTP 200 in 0.96 seconds with a complete JSON `items` array of 24 presets (4,136 bytes), while the device reported an existing BLE connection. This is an isolated observation, not a dashboard cold-load, concurrent-controls or device-performance acceptance. The peer was not disconnected and no lighting or preset changes were made.
