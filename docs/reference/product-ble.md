# Product BLE control: RPC2

RPC2 uses the existing BLE UUIDs and version 1 LF framing, with explicit service epochs, client sessions and complete product scenes. Identity must advertise `rpc:2` before an app uses this contract. The product working tree implements this dispatcher. The [implementation plan](../work/present/Plan-20261026%20-%20HELIO%20NEBULITE%20battery%20and%20groups.md) records firmware, app and device acceptance.

## Identity, radio and security

The historical UUID family comes from AuraviaLTD/WLED `ble`, commit `6e18e1a098e4381c3f897a11652c1be66410280b`, `usermods/Ble2Json_v2/ble_const.h`. Historical Arduino BLE2JSON, Nordic UART/DFU and WLED HTTP dispatch are separate protocols. The product uses these distinct family members:

| Attribute | UUID | Access |
|---|---|---|
| Service | `bee30700-a2eb-4f7a-889b-13192c8c1819` | Advertised |
| State/result | `bee30701-a2eb-4f7a-889b-13192c8c1819` | Read, notify |
| Command | `bee30702-a2eb-4f7a-889b-13192c8c1819` | Write with response |
| Identity/capabilities | `bee30703-a2eb-4f7a-889b-13192c8c1819` | Read |

Identity contains `v:1`, `rpc:2`, `firmware`, `build`, canonical hardware `id`, `maxRequest:512`, `wifiRequired:false`, `wifiToggle:true`, `bleOta:false`, `groups:"scheduled-patch"`, `ota:"http-app"`, `batteryOp:true` and `bas:0..2`. It fits 256 bytes. Match `id` to the hardware MAC reported by System over HTTP; a BLE radio address or friendly name alone does not establish the previously known device.

The working-tree scan hint adds a versioned 13-byte service-data body to the legacy scan response. It carries registered product and scene codes, actual LED count, an explicitly configured sphere diameter, and known power, speed, RGB, white and palette values. Unknown fields use the corresponding flags or registry sentinel; diameter zero means unknown. These public hints are quantized and may be stale: they do not authenticate the device, reproduce a complete scene, or replace connected RPC2 state. The HELIO target supplies product code 1. On NEBULITE-S3 and NEBULITE-ESP32, the exact selected Croptop and Jacket setup names supply codes 20 and 22 respectively; other names, such as the ESP32 Fanny pack and Backpack, remain unknown. The selection is a user-configured hint, not proof of physical wiring. This implementation still requires native radio, BAS coexistence and app discovery qualification before release.

ESP32 uses native ESP-IDF NimBLE as a peripheral; the app is the central. Other platform backends report BLE unavailable. The S3 source candidate configures eight simultaneous centrals and the classic ESP32 candidate two; physical multi-phone, memory and radio-coexistence acceptance remains pending. Disconnect discards only that connection's partial input and restarts advertising when capacity permits, while accepted scheduled work and retained sessions survive. Normal tree preparation preserves the host and epoch; releasing/disabling BLE stops it. A fresh service startup generates a new 128-bit epoch.

After connecting, the current ESP32 source requests four seconds of link supervision when a valid central-selected timeout is shorter. It preserves the negotiated interval and latency; the firmware does not terminate a connection when the request fails. A request is not confirmation that the central accepted it, nor proof that it prevents a link timeout.

On the 29 October bench image `759d30f2…`, a Mi9 app and a Linux client connected at the same time, received distinct sessions in the same epoch, and Mi9 obtained correlated read-only results while Linux remained connected. This establishes that observed overlap, not a two-phone or concurrent-write acceptance. The subsequent reconnect sequence and Linux final read did not complete before the test timeout. The newer source's advertising restart change has not been installed in this image.

The current source candidate exposes lighting GATT reads and commands without requiring pairing or encryption; public identity reports `security: "open"`. Any nearby compatible client can control the permitted functions, including the Wi-Fi on/off switch. Files and firmware upload are outside this BLE channel. A Wi-Fi password travels only on a link the phone encrypted, as [Wi-Fi setup](#wi-fi-setup) describes. Epochs, sessions, challenges and group names correlate work; they are not account ownership, an exclusive owner lock or additional authentication. A separate optional access-control design remains unimplemented. Existing bonds are not deliberately deleted; the behavior of existing and newly connecting phones requires device testing.

The RPC peer container is capacity-parameterized: a host test exercises eight independent sessions, retained replies and one disconnected client's resume while the other seven remain active. The transport peer table, active-owner snapshot and RPC session allocation share the configured radio capacity, with a compile-time maximum of eight. The S3 source configuration requests eight simultaneous links and nine controller activities; the classic ESP32 configuration remains at two links. Runtime RAM, packet buffers and coexistence with Wi-Fi and LED rendering need physical qualification; this is not an eight-phone acceptance claim.

On ESP32-S3, `CONFIG_BT_CTRL_BLE_MAX_ACT` counts controller activities, including advertising as well as connections; it is not a bond limit. The pinned ESP-IDF describes 828 bytes of controller memory per activity. The classic ESP32 uses the separate `CONFIG_BTDM_CTRL_BLE_MAX_CONN` setting. A higher host connection count therefore also needs an appropriately sized controller configuration; changing the host setting alone cannot establish the requested capacity.

Earlier bench images required encrypted GATT and persistent bonding. Their `repairPairing` button authorizes one stale-bond replacement for three minutes, limited to the active peer and guarded against a security downgrade. Those installed-image results and recovery limits are recorded in the [repair plan](../work/present/Plan-20261027%20-%20BLE%20re-pair%20recovery.md); they do not establish acceptance of the open candidate.

29 October, on the identified bench sphere `34:CD:B0:37:4E:A4` / BLE `34:CD:B0:37:4E:A6`, Linux BlueZ reported this central neither paired nor bonded before and after a read-only session. With pairing disabled in the client, identity reported `security:open`, and a fresh nonce-correlated RPC2 hello plus epoch/session-correlated `get` succeeded. The lamp reported off. One earlier connection dropped during service discovery before a bounded retry succeeded. Two later attempts to run a no-change live write also disconnected during service discovery, before any RPC write; the write remains untested and was not retried blindly. No HCI capture or fresh-phone control write is claimed. This tests the installed bench image, not every build or multi-phone behavior.

A subsequent ten-second passive scan saw one target advertisement at −85 dBm from the Linux adapter across the wall. This is a single weak-reception observation, not a measurement of the connection's loss rate or proof of the disconnect cause. Initially unprivileged `btmon` could not bind its monitor channel. The four-second supervision request described above belongs to the newer source candidate and was not part of that installed-image test.

A subsequent scoped no-change-write attempt first disconnected during discovery. The next connection read identity and completed hello, but its correlated `get` read returned ATT `0x0e` before the lighting patch was sent. The attempt stopped without retrying a write; HTTP readback still reported off and brightness 252, and BlueZ reported unpaired, unbonded and disconnected. This distinguishes one observed read failure from a confirmed rejected or applied lighting command; it does not establish whether signal, service load or another cause produced it.

A later bounded, non-pairing diagnostic used `btmon` with an existing noninteractive privilege. The central connected with a 48.75 ms interval and 420 ms supervision timeout. Discovery completed, then a Device Name read was followed by HCI disconnect reason `0x08` (connection timeout), before an application identity read or lighting command appeared on the captured link. BlueZ reported `0x0e` to the client for this attempt; the capture does not show an on-air ATT `0x0e` response, so it must not be attributed to the HELIO callback. This capture establishes a short-timeout link failure for that connection, not the cause of every previous failure. Raw capture is private under `ble-open-hci-20261029/capture.btsnoop` in the external firmware backup capsule.

The isolated bench image `a0452be9` was then installed on the same sphere with the timeout request as its only source change. Its runtime build reads `Oct 29 2026 06:11:31`. A new HCI capture shows the Linux central accept a change from 420 ms to 4,000 ms, and the controller's connection-update-complete event confirms 4,000 ms; identity, hello and `get` succeeded without pairing. A separate unpaired session received terminal `applied` for a single live `on:false` request while already off and confirmed unchanged power, brightness, palette and Wi-Fi afterwards. BlueZ remained unpaired and unbonded. These two finite sessions do not prove phone behavior, simultaneous clients or sustained reliability. The first OTA recovery guard stopped because the application-descriptor build time differed from runtime `build_info`, and a transient network discovery row aged out. A separately guarded flag recovery verified protected configuration and all 25 native preset files; no automatic retry or reboot was issued. Evidence is in `timeout-bench-20261029/ota-single/acceptance-summary.json` and its private HCI capture in the external capsule.

The current source candidate adds a read-only `link diagnostics` service control with the current number of links and configured capacity, aggregate successful connections, failed connections and disconnects, plus the last native disconnect reason and connection-update status (`-1` until observed). It also counts two callback paths that return ATT `0x0e`: a state read with no matching live peer and an unexpected characteristic operation. It stores no peer identity or key and resets when the BLE host restarts. The current count does not identify phones; the counters are independent snapshots and can change during a read. A status of zero reports an update event, not proof that a particular supervision request was accepted. These are software counters; a zero does not rule out an error returned elsewhere in NimBLE. They do not measure signal quality or determine cause. The control has not yet been exercised on a device.

In the two-central source candidate, scoped legacy repair is not admitted while two peers are connected. If the repair peer disconnects while another remains, the authorization window is cancelled rather than transferred to that other peer. The repair and ordinary reconnection paths require native multi-peer qualification.

The earlier bonded images can refuse pairing at full bond capacity. FULL counters count prechecks rather than completed requests. The open candidate does not require the phone to create a bond to use lighting; its behavior with full legacy storage and both previously bonded and fresh phones still needs physical acceptance.

## Framing and result snapshots

Send one UTF-8 JSON object followed by LF, at most 512 bytes before LF. Write sequential fragments of at most negotiated ATT MTU minus3, waiting for every write response. No operation dispatches before LF. Empty/NUL/oversized input and multiple lines in one write fail. An invalid partial line drains to LF; after an ambiguous fragment write, reconnect before starting another request.

An ATT acknowledgement is transport receipt, not execution. Read State/result for a correlated reply. Notifications carry little-endian uint32 publication revision followed by uint32 device uptime, eight bytes total; they are read hints. A missed notification is repaired by reading. At the first offset-zero read, the server captures a whole response and retains it through subsequent Read Blob offsets. A concurrent publication cannot mix response fragments within that long read.

Every generated reply uses this common envelope:

| Field | Meaning |
|---|---|
| `v` | Framing version 1 |
| `id` | Parsed uint16 request ID; zero when malformed framing/metadata prevents trusted correlation |
| `ok` | Whether this operation's dispatch/result succeeded |
| `now` | uint32 device uptime in milliseconds |
| `e` | Actual current server service epoch, 32 lowercase hexadecimal characters |
| `s` | Requested session, or zero when absent; a successful hello returns its established session |
| `r` | uint32 generated-result revision |
| `op` | Operation name, or `error` when unavailable |
| `error` | Error string when `ok:false` |

Responses also fit 512 bytes. Exact cache replay preserves its original result bytes and `r`; the notification's publication revision need not equal that cached `r`. Revision alone proves neither freshness nor execution. Validate canonical device identity and the expected epoch/session/ID/operation. A new epoch or `session-expired` requires a new handshake rather than indefinite ID matching. Errors retain the common envelope, with the actual server epoch even when the request supplied an expired one.

## Hello, resume and retries

Hello is the only operation that omits `$e` and `$s`. It carries `$v:1`, `$id:1..65535`, `$op:"hello"` and a fresh 32-character lowercase-hex `$nonce`. Its challenge is echoed on success and on errors for a well-formed 32-character challenge, so a previous central's readable hello cannot satisfy the new one. For example:

```json
{"$v":1,"$id":1,"$op":"hello","$nonce":"102132435465768798a9bacbdcedfe0f"}
```

```json
{"v":1,"id":1,"ok":true,"now":1000,"e":"00112233445566778899aabbccddeeff","s":1,"r":2,"op":"hello","nonce":"102132435465768798a9bacbdcedfe0f","hi":0}
```

Epochs, sessions, timestamps, revisions and catalog hashes in examples are illustrative. Copy values from the identified device. Hello IDs are outside the normal request namespace; normal IDs may begin at1, with2 convenient after a hello using1. `hi` is the highest accepted normal request ID in that session.

A new challenge without resume creates the next server session and clears its one-entry retry cache. Repeating the current challenge is idempotent and preserves the session and `hi`. Explicit `$resume:{"e":"00112233445566778899aabbccddeeff","s":1}` succeeds only for a retained session in the current epoch, preserves `hi` and the cache, and echoes the supplied challenge. An active session cannot be resumed by a different connection. Retained disconnected sessions may expire when their bounded slot is reused; an expired resume fails. A new session preserves already accepted scheduled work under that work's original session and ID.

Every other request requires `$e`, nonzero uint32 `$s`, `$v:1`, `$id:1..65535` and `$op`. Normal IDs increase strictly within a session. Exact bytes under the last accepted ID replay its retained result without reapplying. Changed bytes under that ID fail `id-reused`; an older ID fails `id-expired`. After 65535, establish a new session with a new challenge. The cache is one result, not a transaction ledger; query a retained job with `status` after advancing to newer requests. This correlation rule covers reads as well as writes. Unscoped version 1 reads/writes have no RPC2 fallback.

## State and device metadata

`get` and `clock` return the same compact master-state shape; `clock` supplies the current uptime for scheduling. For example:

```json
{"$v":1,"$e":"00112233445566778899aabbccddeeff","$s":1,"$id":2,"$op":"get"}
```

```json
{"v":1,"id":2,"ok":true,"now":1100,"e":"00112233445566778899aabbccddeeff","s":1,"r":3,"op":"get","state":{"on":true,"brightness":96,"palette":0,"wifiEnabled":true,"ambientTarget":null,"ambient":{"service":"Ambient Light","automatic":true}},"queued":false,"group":"","pending":null,"terminal":null}
```

`state` contains available actual master `on`, `brightness`, numeric `palette`, `wifiEnabled` and `ambientTarget`. Missing controls remain omitted; `ambientTarget` is null without an active valid sensor override. `ambient` names the enabled ambient-light service and its `automatic` switch, which a `patch` of `{"Services":{"<service>":{"automatic":false}}}` turns off; it is absent without such a service. `queued:true` means a job waits for its deadline. During preparation it becomes false while `pending.state` is `applying`; it does not prove completion.

`info` returns `model`, `ledCount`, `channels`, `basChannels`, `basRestartRequired`, `profileOp` and `wifiOp` in its common envelope, and `setupOp` on a firmware that offers [product setup](#product-setup). The current source additionally provides `firmwareVersion` and `builtAt` strings; older installed images omit them. When ESP32 reports a distinct running and next app partition, source candidates also include `otaSlotBytes`, the target partition capacity in bytes. The field is omitted if that capacity cannot be established. It neither authenticates a release nor proves that an update will boot or can be recovered. These labels help distinguish build attempts but are not an image digest, authentication or a reliable substitute for verified release metadata. Output counts are zero and model is empty when those sources are unavailable. These are reported capabilities, not hardware-profile qualification. `effect` returns `effect:null` or the first enabled effect's actual `name`, `type` and available numeric `red`, `green`, `blue`, `white`, `speed`, `brightness`. This summary does not describe a composite scene; use `scene-state` for canonical complete-scene recognition.

An `info` exchange from the source candidate, with illustrative session and time values:

```json
{"$v":1,"$id":2,"$op":"info","$e":"00112233445566778899aabbccddeeff","$s":1}
```

```json
{"v":1,"id":2,"ok":true,"now":1000,"e":"00112233445566778899aabbccddeeff","s":1,"r":3,"op":"info","ledCount":222,"channels":4,"model":"HELIO 1","firmwareVersion":"7.0.0-dev","builtAt":"Oct 29 2026 03:45:00","basChannels":0,"basRestartRequired":false,"profileOp":true}
```

When `info.profileOp` is true, `profile` reads the versioned product descriptor as a separate bounded RPC2 response. Schema 1 codes use the scan-hint registry: 1 HELIO 1, 2 MINI80, 20 Crop-top S3, 22 Jacket/Vest S3. Code 2 is prepared for a future MINI80 firmware target; no qualified MINI80 target is activated by this descriptor. The source is `firmware-target` for HELIO or `selected-model` for a garment. Both are configuration hints, not physical attestation; `verified` remains false. Unknown or custom products return `profile:null`. `diameterMm` is null unless explicitly configured for a sphere, and is never inferred from LED count. `revision` and `mapping` stay null until qualified. The `info` output count and channel count describe the current output independently; a product code does not establish that wiring matches. Re-read after a model or configuration change, and do not treat cached descriptor results as fresh.

The read-only `productProfile` control of the BLE service exposes the same descriptor as a JSON-encoded string over the existing HTTP/WS module interface. It updates on the service's one-second tick; a missing control or temporarily unavailable value is not a product identity. For `source:"selected-model"`, compare the code against the current `System.deviceModel` and treat a mismatch as unknown while awaiting the next tick. Parse the complete JSON string and re-read after changing product setup. The BLE `profile` operation resolves the current configuration per request.

```json
{"$v":1,"$id":7,"$op":"profile","$e":"00112233445566778899aabbccddeeff","$s":1}
```

```json
{"v":1,"id":7,"ok":true,"now":1000,"e":"00112233445566778899aabbccddeeff","s":1,"r":8,"op":"profile","profile":{"schema":1,"code":22,"source":"selected-model","form":"garment","verified":false,"revision":null,"mapping":null,"diameterMm":null}}
```

## Live and saved changes

`patch` accepts the normal [state document](integrating.md#setting-everything-at-once), with an obligatory boolean `$live`. True selects StateSource::Live; false deliberately changes saved configuration. For example, temporary master control is:

```json
{"$v":1,"$e":"00112233445566778899aabbccddeeff","$s":1,"$id":4,"$op":"patch","$live":true,"Drivers":{"on":true,"brightness":96}}
```

A live change triggers no configuration save. There is no separate persistent shadow tree: a subsequent deliberate save of the same subtree can serialize its then-current live values. Group membership is deliberate persistent configuration rather than a live setting.

An app can send transient changes during a slider gesture, then automatically send its confirmed final setting with `$live:false` after release or debounce. It waits for the final operation's terminal result and surfaces failure. Deliberate tuning persists without a separate Save button; a temporary show or preview remains live. A complete scene operation includes the user's actual chosen controls because omitted controls receive catalog defaults.

Allowed roots are `Drivers`, `Effects`, the single network field `Network:{"wifiEnabled":false}` or true, and scalar controls of existing Audio/BH1750 services. Other Network fields, including `enabled`, fail. A Services patch cannot create/replace/delete services or configure BLE/battery hardware. The Wi-Fi toggle stops station/AP fallback without forgetting credentials; BLE can restore it. Generic MoonLight recovery policy remains separate.

With ambient control active, master brightness is the manual fallback. An explicitly manual operation also patches the actual existing BH1750 service, for example `Services:{"Ambient Light":{"automatic":false}}`. The app must not silently disable automation merely because it writes brightness. Native W remains independent of RGB. Some apply-time errors can follow earlier mutations, so after failure read actual state rather than assuming rollback.

## Wi-Fi setup

When `info.wifiOp` is true, three operations set up Wi-Fi with the device's own radio. They use the network module's scan, its connect-first join, and its cascade, which tries the known networks first and opens the access point when none joins. Each reply carries the same `wifi` object after the common envelope:

| Field | Meaning |
|---|---|
| `enabled` | False while the product's Wi-Fi switch is off; `wifi-scan` and `wifi-join` then fail `wifi-disabled` |
| `ssid` | The network the station has joined or is joining, or null |
| `ip` | The station's IPv4 address once it has one, or null |
| `ap` | The device's own access point name while it is open, or null |
| `join` | How the last join went: `joining`, `joined`, `wrong-password`, `not-found`, `failed` or `refused`; null before the first |
| `scan` | `waiting` for the radio, `scanning` or `idle` |
| `scanAt` | Device uptime in milliseconds when the last scan finished; 0 before the first |
| `networks` | Scanned networks from the offset, each `[ssid, rssi, flags]`: flag 1 marks a secured network, flag 2 a known one |
| `next` | Index of the first network on the following page, or null at the end |

`wifi` takes an optional `$offset` from 0 to 255 and returns as many whole networks as fit the 512-byte response, strongest first. A network that hides its name is left out, and each name appears once. When `scanAt` differs from the first page's, a newer scan finished: read again from offset 0.

`wifi-scan` takes no fields. The device scans by itself only when its access point opens. A scan takes the radio off-channel for a few seconds, which can drop network light input while connected. Read `wifi` until `scan` is `idle`: a `scanAt` newer than the one in the `wifi-scan` reply carries the new result, and an unchanged one means the radio could not scan.

`wifi-join` takes `$ssid` of 1 to 32 bytes and an optional `$password` of at most 63 bytes, which needs an encrypted link. The device joins at once and saves the network only once it joined, as its own page does. Without `$password`, a known network joins with its saved password and an unknown one joins as open. A password given for a known network replaces the saved one only after a successful join. Read `wifi` until `join` leaves `joining`, which takes up to the station's join window plus its wait for a lease. When the network does not join, the device tries its known networks and then opens its access point.

```json
{"$v":1,"$e":"00112233445566778899aabbccddeeff","$s":1,"$id":9,"$op":"wifi-join","$ssid":"venue","$password":"secret-pass"}
```

```json
{"v":1,"id":9,"ok":true,"now":5000,"e":"00112233445566778899aabbccddeeff","s":1,"r":10,"op":"wifi-join","wifi":{"enabled":true,"ssid":null,"ip":null,"ap":"MM-ABCD","join":"joining","scan":"idle","scanAt":4000,"networks":[["home",-48,3],["venue",-60,1]],"next":null}}
```

A password travels only on an encrypted link. On an open link, `wifi-join` with `$password` fails `encryption-required` and the device discards the password, which has already crossed the air, so a client encrypts first. `secure` takes no fields and asks the phone to pair: the device sends an SMP Security Request, and the phone's LE Secure Connections pairing, Just Works without a bond, encrypts this connection until it ends. The reply carries `link`:

| `link` | Meaning |
|---|---|
| `pairing` | The phone was asked and is pairing; it may show its own prompt first |
| `encrypted` | The password may follow on this connection |
| `failed` | The phone declined, or the pairing timed out after 30 s; told once, and the next `secure` asks again. After a timeout the phone may refuse further pairing until it reconnects (Bluetooth Core, Vol 3 Part H, 3.4) |
| `open` | The device could not read the link just now; the next `secure` asks |

Read `secure` about once a second until `link` leaves `pairing`, allowing the time a person takes to accept the prompt. Just Works pairing protects the password from a listener, not from an attacker who relays the connection while it pairs. A join with the saved password sends no secret and needs no encryption.

```json
{"v":1,"id":8,"ok":true,"now":4900,"e":"00112233445566778899aabbccddeeff","s":1,"r":10,"op":"secure","link":"encrypted"}
```

The product's Wi-Fi switch is the `Network.wifiEnabled` patch under [Live and saved changes](#live-and-saved-changes). A build without a network module reports `info.wifiOp:false` and fails these operations with `wifi-unavailable`. Malformed fields fail `ssid`, `password`, `offset` or `fields` before any join starts. `secure` arrived with `wifiOp`, so `info.wifiOp:true` also means `secure` is there.

## Product setup

A NEBULITE firmware boots with Wi-Fi off, so the phone sets a garment up over BLE with the same [device-model catalogue](../../mooninstaller/deviceModels.json) the dashboard offers. When `info.setupOp` is true, `profiles` lists the setup choices that name the running firmware and chip, and `setup` applies one by name:

```json
{"$v":1,"$id":40,"$op":"profiles","$e":"00112233445566778899aabbccddeeff","$s":1}
{"v":1,"id":40,"ok":true,"now":1000,"e":"00112233445566778899aabbccddeeff","s":1,"r":9,"op":"profiles","models":["NEBULITE Croptop ESP32","NEBULITE Fanny pack ESP32","NEBULITE Backpack ESP32","NEBULITE Jacket ESP32"],"queued":false,"group":"","pending":null,"terminal":null}
{"$v":1,"$id":41,"$op":"setup","$model":"NEBULITE Croptop ESP32","$live":false,"$e":"00112233445566778899aabbccddeeff","$s":1}
```

`setup` takes only `$model`, `$live`, `$at` and `$group`. It applies the profile's `System.deviceModel`, `Drivers` and `Layouts` document as a saved change, through the same pending, terminal and readback contract as `patch`. An unknown or unfitting name fails `model` before anything changes; any other field fails `fields`. `profile` and `info.model` then report the selected model. The compiled table is generated together with the dashboard's by `uv run moondeck/build/gen_product_profiles.py`.

## Complete scenes and their controls

Product scene callbacks use one canonical scene source for firmware and dashboard data. The candidate catalog contains the 23 HELIO entries plus `solid`; discover its actual content and generated revision rather than assuming a fixed count or WLED effect numbers. `scenes` takes `$offset`, starting at 0, and returns as many whole entries as fit the 512-byte response:

```json
{"$v":1,"$e":"00112233445566778899aabbccddeeff","$s":1,"$id":5,"$op":"scenes","$offset":0}
```

The envelope adds `catalog`, `scenes:[{"id":"solid","name":"Solid RGBW","audio":false,"rgbw":true},…]` and `next`, the index of the first scene on the following page, or a null next cursor at the end. IDs are stable strings such as `solid` and `colortwinkles`. The `audio` flag identifies the scene's input dependency; it does not transmit phone audio. `rgbw` means the scene exposes native W, while actual output channels come from `info`.

`scene-info` takes `$scene` and `$offset` and returns `catalog`, `scene`, `controls` and `next`. A page holds as many descriptors as fit the 512-byte response, each with `name`, `type`, `default` and numeric `min`/`max` when applicable; a control the catalogue names also carries its display `label`, such as `Intensity` for `effect intensity`. A select or palette descriptor also carries `optionCount`, `optionOffset` and `options:[{"value":0,"label":"…"},…]`, its options contiguous from `optionOffset`. A list that does not fit continues as the first descriptor of the next page, under the same control name with a later `optionOffset`; merge by that name and offset. The cursor counts parts, one per plain control and one per option, so a scene with a 60-entry palette takes about thirteen pages rather than seventy, and the 24-scene catalog five rather than 24. Follow `next` until null. Defaults and scene identity come from the generated catalog; types, ranges and options come from registered firmware controls.

`scene` requires `$scene`, `$catalog`, `$live`, optional `controls` and optional `$at`/`$group`. Controls use their actual names and typed numeric/bool values, including spaces in names such as `effect intensity`. Defaults fill omitted exposed controls; unknown names, wrong types, invalid ranges or catalog mismatch fail before scene application. For example:

```json
{"$v":1,"$e":"00112233445566778899aabbccddeeff","$s":1,"$id":7,"$op":"scene","$live":true,"$scene":"solid","$catalog":"a994af6a8f83","controls":{"red":0,"green":0,"blue":0,"white":32}}
```

The `ripple` scene uses RingsEffect. Its `background brightness` control is uint8, range0–255, default0. It adds palette light at the `hue_shift` position between the rings;0 retains the black background. `max pause ms` is uint16, range0–2000, default500. It limits completely dark mapped RGB-source intervals by starting one fresh ring without changing movement speed;0 starts that ring on the first dark frame. The limit applies at rendered frame boundaries, before dynamic modifiers, dimming and power. A black palette remains black. This complete example keeps slow motion and a black background with at most500ms of source darkness; obtain the current catalog from the identified device:

```json
{"$v":1,"$e":"00112233445566778899aabbccddeeff","$s":1,"$id":8,"$op":"scene","$live":true,"$scene":"ripple","$catalog":"bfe5e37ed69b","controls":{"count":4,"speed":1,"thickness":5,"hue_shift":0,"background brightness":0,"max pause ms":500,"palette":0}}
```

The operation applies one complete internal state document to the selected scene region. It replaces competing editable scene content, restores the layer baseline, preserves the selected layout and output hardware, and disables competing code-wired scene content. Ambiguous/unrepresentable regions fail `scene-region`. It does not split one visible scene into unrelated partial writes. Master controls remain separate.

`scene-state` returns `scene:null` or `scene:{"id":"solid","catalog":"a994af6a8f83","controls":{"red":0,"green":0,"blue":0,"white":32,"brightness":255}}` in the common envelope. Recognition reads actual complete structure and control values. An incompatible layer baseline, competing active content or unknown composition produces null; the last requested ID is not used as proof. Cache scene metadata against identified firmware/build and catalog, and rediscover after a firmware change.

For catalogue `5022ee54a0b8`, the app's current source adapter and compiled blink timing/palette functions agree on 10,080 RGB pixels across 100 nominal-frame cases, including different strip lengths, segment limits and blend modes (`moondeck/helio/check_app_blink.py`). This is a source-preview comparison, not physical output, missed-frame timing, the independently driven white channel or proof that the installed app and firmware include this catalogue.

## Groups, pending work and cancellation

`group` with `$group:"party"` saves membership; an empty string leaves. Names are at most 32 UTF-8 bytes. A patch/scene carrying `$group` must match nonempty local membership. The optional `$at` is a uint32 device-uptime deadline, no more than 60 seconds ahead or 250 milliseconds late. Derive it from a fresh device clock estimate. One queued/applying job is held; another patch/scene returns `busy`.

`get` exposes `pending:null` or `{s,id,state,due}`, with state `queued` or `applying`. `terminal` retains one `{s,id,state,error?}`, with state `applied`, `cancelled` or `failed`. These identities belong to the current service epoch. Disconnect or a new session does not erase a pending job. A software reboot/service release clears the volatile job/result history.

Both `status` and `cancel` require an explicit target; the request itself uses its current session and increasing ID, independently of the target's earlier session:

```json
{"$v":1,"$e":"00112233445566778899aabbccddeeff","$s":1,"$id":11,"$op":"status","$target":{"s":1,"id":10}}
```

The envelope adds `target:{"s":1,"id":10,"state":"queued","due":6000}`. Cancel with the same target cancels only a queued match. A matching applying job reports `applying`; it is not reported cancelled. An applied terminal target reports `already-applied` to cancel and `applied` to status. Retained cancelled/failed states keep those names and any error. A missing target returns `not-retained`, not success at cancelling unknown work. Untargeted cancel fails `target-required`.

Due work runs before handling another command at the same instant. A cancel racing its deadline therefore observes application already started or applied. `applied` follows required frame preparation/deferred-control handling; it is not an ATT acknowledgement or proof of physical light. Preparation results aggregate the rebuild's deferred writes, so an error in that aggregate can conservatively fail the operation. Failure can leave a partial change; follow with state/scene readback.

Each service tick uses one uptime sample and processes due work once before taking the next request. A deadline or preparation completion reached during that request is observed on the next tick; its response is published separately instead of being overwritten by the other peer's reply. A host transport seam covers this ordering, not radio delivery or physical group synchrony.

A separate host integration test runs two peer mailboxes and two RPC sessions: one peer's scheduled group patch reaches its deadline while the other reads. The originating peer keeps its correlated terminal response and the other receives its own correlated read. This uses the actual session, scene service and peer-table code; it is not an on-air two-phone test.

The app estimates every member's clock and round-trip uncertainty, converts a common desired start to each device uptime and reconciles execution results. Preserve uint32 wrap handling and per-member failures. Scheduled starts do not make BLE a mesh or share animation phase, random state or audio. Basic3/4/5 mixed groups and wearable mini-HELIO require their own app/device acceptance; a single HELIO test proves no measured group skew.

Desktop tests with two independent firmware instances exercise different local
clock origins, a shared deadline and a per-member group mismatch; the accepted
member executes while the mismatched member reports its own error. These are
deterministic scheduler tests, not a measurement of BLE latency, app clock
estimation or synchronization on physical lights.

## Battery telemetry and standard BAS

`battery` returns only `battery:{sampling,packs}` after the common envelope. RPC2 intentionally puts BAS flags in identity/`info`, not in this two-pack response, to retain the 512-byte bound. Each pack has physical `id:1` or2, `sample` and `charging`. No configured packs yields an empty array. This fresh-value example illustrates an explicitly configured voltage estimate, not a measurement from the mains-powered bench sphere:

The current app decoder accepts only `charging:"unknown"` and rejects other values. A future qualified charge-status implementation therefore requires a coordinated, versioned app/firmware contract before it emits another value; adding a GPIO read on the firmware alone would break battery decoding. The historical S3 charger-status nets and their logic levels are not yet physically qualified. USB power or a measured battery voltage alone does not establish whether a cell is charging.

```json
{"v":1,"id":13,"ok":true,"now":7000,"e":"00112233445566778899aabbccddeeff","s":1,"r":14,"op":"battery","battery":{"sampling":false,"packs":[{"id":1,"sample":{"presence":"present","state":"fresh","voltageMv":3600,"percent":50,"ageMs":0,"estimate":"voltage"},"charging":"unknown"}]}}
```

Presence is `unknown`, `present` or `absent`; measurement state is `unknown`, `fresh` or `stale`. Missing measurements use null. A stale sample retains its last voltage and age but has null percentage. Percent requires explicit presence, fresh measurement and configured voltage-model endpoints; `estimate:"voltage"` labels that estimate. Charging stays unknown without a qualified status detector. HTTP/WS exposes the same pack shape through each BatteryChannel's read-only `sample`. Device sample age and local app receipt age are independent; disconnect makes retained app observations stale. Groups preserve member/pack values without an invented average.

The web control's sample envelope additionally carries a changing `revision` on each telemetry refresh. The dashboard uses its local receipt age to hide a percentage if updates stop for over 12 seconds, even while the WebSocket still appears open; its five-second check and browser timer throttling can delay this transition. A later revision restores the display. Older firmware without this field remains compatible but cannot provide that extra half-open-connection check. This web-only field is not part of the BLE battery response.

An invalid channel configuration returns `battery.error:"invalid-configuration"` with an empty `packs` array. This is a configuration error, not evidence that the product has no battery; clients must surface it rather than interpreting it as a mains-only product.

When a standard battery notification cannot be queued, the ESP32 retries the latest valid value at the next battery publication tick for current subscribers. A successful queue operation is not a delivery acknowledgement. A stale or unknown reading has no Battery Level byte, so a system UI that caches an older notification may continue to show it; clients needing freshness use the RPC2 sample and age rather than treating that cached byte as current.

Optional standard Battery Service `0x180F` exposes read/notify Battery Level `0x2A19`. Descriptor `0x2904` identifies first/second physical slots even when only slot 2 exists. BAS levels are 0..100; known absence reads/notifies zero, while custom presence distinguishes it from an empty installed pack. Unknown/stale levels return an ATT error and send no invented percentage. A previously notified BAS level cannot be explicitly retracted by this characteristic: a phone may keep displaying its last value until it reads again or disconnects. The app must use the RPC2 sample's presence, state and age for its own freshness decision and must not infer fresh data from the system battery icon. The optional Battery Level Status characteristic is omitted. BAS declarations are fixed until service restart; `info.basRestartRequired` reports a changed configured slot list.

On image `a29d4f7f`, Linux bonded checks cover 0→1→2→0 BAS declarations, stable lighting handles, presentation descriptors, unknown-level errors and declared-absence notifications. Those configurations leave ADC/gate pins unset. They do not establish nonzero battery accuracy, actual charging, RPC2 acceptance, Android cache behavior or Bluetooth qualification. Android system battery display is optional; the app owns its product indicator. See the [Bluetooth SIG Battery Service](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/BAS_v1.1/out/en/index-en.html).

## Profiles, updates and acceptance

The current HELIO fixture is ESP32-S3 N8/no-PSRAM, GPIO35, 222 SK6812 RGBW pixels. Product callback availability and a compiled profile do not identify a wearable's exact PCB, divider, charger or LED mapping. Classic ESP32, two-charger S3, minimal S3 and MINI80 have separate source/profile and physical acceptance boundaries. No old WLED battery offset, charge curve or old `helio1` mapping is implicitly qualified.

BLE firmware upload is not implemented. Firmware exposes an HTTP application-image upload used by guarded bench tooling; a working MoonLight update flow in the NEBULITE app has not been established. New product builds carry an optional firmware-variant descriptor. The new HTTP upload code rejects a tagged different product variant before writing; the URL/HTTPS path checks the downloaded inactive slot before selecting it for boot. Untagged historical images remain accepted for compatibility. Existing installed firmware cannot enforce either new check. The descriptor is not a cryptographic signature or a physical-board identifier. In particular, a controller without Wi-Fi has no qualified update route through this BLE protocol. Historical combined ZIP images belong to a separately qualified USB migration, not an app-slot upload. Verify chip/flash/partitions/build identity and recovery before updating a real product. Interrupted-update recovery, physical power cycles, all supported product boards, app pairing/reconnect/notifications, genuine battery calibration, optical previews and multi-device groups remain independent release evidence. The existing [bench record](product-bench-20261025.md) attributes earlier tests to their exact images. No customer flash or public upstream acceptance follows from this working-tree protocol.

Indirect control-surface target writes through the `Control` root are outside this RPC channel. Use the explicit lighting/scene operations; a surface binding could otherwise redirect a lighting command into disabling BLE itself.

The checked-in classic 4 MiB MoonBase layout has one large application slot and a separate factory recovery slot. While that application is running, the OTA writer refuses to overwrite its own slot; recovery must run before an app image can be replaced. The current MoonBase build provides network recovery, not this BLE RPC service or a BLE image receiver. Consequently the experimental BLE upload building blocks in the main app cannot on their own update that single-slot layout offline. An app-only BLE updater cannot bridge this reboot: a separately designed, authenticated and qualified recovery transport or another verified recovery path is needed. This describes source topology, not the partition table or recovery image actually installed on a customer's controller.

## Configured point-map pages

Source candidate: when `info.pixelMapOp` is `true`, `pixel-map` reads the currently configured single enabled `PixelMapLayout`. It describes layout coordinates in physical output order, including non-RGB slots; it is not LED identification, authentication, physical calibration, a live frame, or the Layer's post-modifier/reduced raster. Composite or non-point layouts return `map:null` on the first page. Older firmware omits the capability and must not receive this operation on that basis.

A first request omits `$rev` and starts at offset zero. Each response holds at most 16 coordinate pairs and a `revision` of 32 lowercase hex digits: SHA-256 of the exact stored point string truncated to 128 bits. Repeat that token on every continuation. A changed or removed map returns `map-changed`, so the client discards the old pages and starts a fresh read, including when the new map is shorter than the previous cursor. With the current revision, an offset at or beyond the count gives `map-offset`; malformed revision gives `map-revision`; invalid mapping gives `map-invalid`. The operation cannot change a layout or light. Every success or error still uses the normal epoch, session, request and revision envelope. A null `next` ends the read. For the 31-slot prototype jacket point string, a first page and its continuation look like this:

```json
{"$v":1,"$id":2,"$op":"pixel-map","$e":"00112233445566778899aabbccddeeff","$s":1,"$offset":0}
{"v":1,"id":2,"ok":true,"now":1000,"e":"00112233445566778899aabbccddeeff","s":1,"r":3,"op":"pixel-map","map":{"kind":"pixel-map-2d","revision":"982625c70e1f5ba463fe4a79aeb9732a","total":31,"offset":0,"points":[[5,14],[7,14],[9,14],[11,14],[13,14],[15,14],[5,6],[7,6],[9,6],[11,6],[13,6],[15,6],[7,2],[8,1],[9,1],[11,1]],"next":16}}
{"$v":1,"$id":3,"$op":"pixel-map","$e":"00112233445566778899aabbccddeeff","$s":1,"$offset":16,"$rev":"982625c70e1f5ba463fe4a79aeb9732a"}
```

The App source contains a capability-gated, paged reader. An offline compatibility check feeds actual firmware replies to the App decoder and controller, including a changed-map restart; session setup is simulated. The map does not identify which slots are RGB LEDs or a PWM white output, and it must not alone be used as a garment wiring map. The firmware operation and App reader are not yet installed or physically tested; radio, reconnect and preview integration require qualification before use as a customer feature.

## Configured output windows

Source candidate: `info.outputsOp:true` advertises the read-only `outputs` operation. Each response has an `outputs` object with `revision`, `total`, `offset`, `drivers` and `next`. Request page zero without `$rev`; request each following offset with the prior 32-character revision. A changed configuration yields `outputs-changed`, so discard the old pages and restart. An empty active output tree has `total:0`, an empty array and `next:null`. Responses contain at most one active driver per page; inactive drivers and the software-only `PreviewDriver` are excluded.

Each entry contains its module `type`. Known `RmtLedDriver` and `PwmLightDriver` entries also contain configured `start` and `count`, with count zero meaning the remainder of the source buffer, plus `rgb` (all three resolved RGB fixture channels present) and `white` (resolved white or warm-white fixture channel present). Unknown driver types deliberately have no inferred window or channel roles. The resolved fixture is a software configuration: a window can extend beyond actual driver capacity, overlap another window or fail electrically, and this data does not prove pins, physical emitters, slot exclusivity, or garment wiring. Never assume the first matching window is the only driver for a point. A client may use these descriptors to label uncertain previews, not assert physical RGB/white positions.

The revision covers the described active entries, not unrelated controls or physical changes. `outputs-offset`, `outputs-revision`, `outputs-changed`, `outputs-too-large`, and `outputs-unavailable` report invalid cursor, malformed token, stale continuation, bounded-response failure, or absent reader. The existing generic envelope is unchanged. Firmware/App transport compatibility has only been checked offline; no installed firmware or real BLE exchange is established.

The current App source has a capability-gated output reader and uses the complete descriptor snapshot in its configured-garment preview. The offline `moondeck/helio/check_app_outputs.py` test passes real firmware dispatcher replies, from actual RMT and PWM driver classes with the jacket profile's RGB/White fixtures and windows, through the actual App decoder, reader, controller and preview model. It confirms that an overlapping white slot retains both matching windows, an additional unknown driver prevents an exclusive inference, changed pages restart, and absent capability causes no request. The test uses a simulated scheduler, RPC transport and driver tree; it does not establish that the installed device or the rendered App matches the physical garment.

## Experimental rendered-frame pages

In this source candidate `info.previewFrameOp:true` advertises `preview-frame`. The first offset-zero request starts a pull capture if no current snapshot exists and receives `frame-pending`; retry later. A ready reply contains `frame` with a 32-character `revision`, `total`, `offset`, `rgb` (lowercase hex, three bytes per output), and `next`. Pages contain at most 32 outputs and preserve the PreviewDriver source buffer's output-index order. Follow `next` with `$offset` and `$rev`. `frame-changed` means another reader or a later capture replaced the frame: discard all pages and restart. A missing/stale capture returns `frame-pending`; malformed tokens return `frame-revision`; an invalid cursor returns `frame-offset`. A contended nonblocking snapshot returns `frame-busy`. The producer never waits for this reader. The last page asks for a new capture; the next frame can still have the same revision if the render task has not captured yet. Clients should discard duplicate frames, stop polling while the view is hidden, and give lighting commands priority. Snapshots expire after five seconds. Sources with more than 512 outputs return `frame-too-large`; a disabled or absent preview driver returns `frame-unavailable`.

These are actual composited source colors, with the existing web-preview white-point conversion, before driver-specific brightness, electrical output and physical optics. They are not proof of light emitted. The frame is not atomically tied to a point-map revision. A client must discard frames on configuration changes and treat geometry as approximate; comparing point-map revisions before and after a read reduces but does not eliminate concurrent or transient mapping ambiguity. All color slots, including PWM or unknown output roles, remain in order. The App worktree contains a reader and configured-garment consumer; this is not a released or physically verified app. `moondeck/helio/check_ble_preview_frame.py` exercises the actual preview driver and dispatcher for 65 RGBW and 222 RGB outputs, frame replacement, invalidation and the capacity boundary with an offline buffer. `moondeck/helio/check_app_preview_frame.py` feeds these real replies through the App decoder, reader and controller, including a changed-frame restart. No radio or device validation is claimed.
