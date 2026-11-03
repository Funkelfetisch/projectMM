# BLE commissioning and control

## Use case

A phone controls a portable light's power, brightness, RGBW and scenes when no router is nearby. Wi-Fi is optional during ordinary use; the same device can join a mixed group of lights. The initial upstream use case is different and narrower: set up Wi-Fi through Improv over BLE, then run existing IP control. Product-specific scenes and groups remain outside this first upstream step.

This is a proposal in response to MoonLight PR 158, not an assertion of upstream agreement or shipped behavior. The HELIO product currently uses an open, no-pairing RPC2 test image; the secure upstream design below is not implemented by that image.

## Module and lifecycle

Add a Bluetooth module under Network, beside Improv, with on/off, name, advertising, connection and pairing state. BLE is a control channel rather than an IP fallback. A disabled module creates no NimBLE host task and owns no dynamic BLE buffers. Define explicit stop, restart and partial-start failure ownership, including a failed host stop; do not free resources while callbacks can still use them. Document host task core, priority and Wi-Fi coexistence from the actual ESP-IDF configuration, and measure them rather than assuming the defaults.

Improv is the first client. Its Wi-Fi commissioning operation should reuse Network's existing validation and state application; secrets must not appear in logs or unencrypted characteristics. One later state-document adapter should reuse the HTTP/MQTT state-document path rather than implement another control model. It needs bounded paging and explicit consistency, error, reconnect and flow-control rules before accepting documents larger than one ATT attribute.

## Access and pairing

The maintainer requested a time-limited pairing window opened by an existing trusted UI or physical action, a filter accept list outside that window, encrypted identity and LE privacy. On a device with an actual display or web session, a random passkey shown there can provide authenticated pairing; on displayless hardware Just Works cannot provide MITM protection. Physical window opening limits admission but is not proof of a human-readable passkey. Agree recovery, phone replacement, transfer and full bond-store behavior without silently evicting other owners before making a security claim.

The product owner's preference is frictionless lamp control without a small stored-phone limit. HELIO's existing open test channel implements that tradeoff and exposes no credentials. Do not extend that open channel to Wi-Fi credentials or full state documents. Whether upstream accepts an explicitly selected open lighting-only mode alongside its protected commissioning mode is an open maintainer/product decision, not an assumption; retain ordinary offline control and do not require an owner account.

## Builds and measured costs

Proposed upstream inclusion follows the maintainer's target gate: supported radio, PSRAM and app partition greater than 2.5 MB, with the requested classic ESP32-PICO exception only after its internal controller memory is measured. Heap placement is subject to the SDK's capability constraints; not all radio/controller allocations can be assumed to move to PSRAM. Measure per target: disabled and enabled flash, static/dynamic internal and external RAM, largest free block, task stack and startup/shutdown, plus failed enable and repeated cycles. No target gets a production-fit claim from compilation alone.

Compare Art-Net over Wi-Fi with BLE off, advertising, connected and active under the same offered packet rates and runtime. Record packet loss, frame cadence, heap, task CPU and reconnect behavior. Check real devices for coexistence and user-visible light output. The current HELIO board has no PSRAM; its separate product build cannot establish upstream PSRAM target acceptance.

## Contribution sequence and evidence

1. Agree the plan, target list, pairing/security and recovery policy with maintainers.
2. Rework the existing transport as the first scoped contribution: shared `TryLock`, transport-neutral line framing, module-local documentation, UUID attribution and proper-name spelling exception. Correct the stop-failure lifetime and test it. Keep product UI and hardware profiles out of that contribution.
3. Implement the Network module and protected Improv flow with bounded inputs, lifecycle/error tests and real phone/device commissioning. Add the measured on/off and Art-Net results.
4. Design and test paged state documents separately, then add higher-level product control only with a bounded, agreed wire contract.

Current evidence is narrower: the product bench image has open RPC2 and demonstrated simultaneous Mi9/Linux read-only sessions; its reconnect portion timed out. Host tests and builds do not prove Improv, authenticated pairing, PSRAM placement, Art-Net coexistence, upstream acceptance or physical commissioning. The contributor must personally satisfy the project's line-by-line responsibility requirement; AI review or disclosure is not a substitute for the maintainer's requested attestation.

In the private product source, `bleControlStop` now returns a result and logs
the NimBLE stop error. On failure it retains the handle, callback and context;
the caller reports the incomplete shutdown and can retry while it remains alive.
An extracted actual ESP32 stop-body test injects a failed stop followed by a
successful retry, verifying that callback storage is not cleared early. The
desktop RPC2 tests pass 22 cases / 1,624 assertions, the Python suite passes
698 with five skipped, and NEBULITE S3 and classic ESP32 targets build. The
seam does not inject a failure into the real ESP-IDF host. If the owning
service is destroyed after a failed stop, the private product implementation
now transfers the context to platform ownership; a later start retries the
stop before creating a replacement and cannot reap a host still owned by a
live service. A second extracted-source seam exercises repeated failure and
successful retry, and both S3 and classic builds pass. During a failure the
radio may still be active; this is not a tested device recovery or proof it is
off. The public PR's independent branch has not been modified by this fix.

## Product decision: unpaired control and responsive gestures

Veit explicitly chose lighting control without mandatory pairing. The private product candidate already advertises `security:open`, uses unprotected lighting GATT attributes and disables bonding; the limited Linux bench observation does not establish Android or multi-phone acceptance. This decision does not authorize publishing credentials over the open lighting channel or claim encryption. It does not delete existing phone bonds. The upstream maintainer's separate protected-commissioning proposal remains a distinct discussion.

For slider gestures the app keeps at most one admitted command and one replaceable newest pending value, then sends that newest value as soon as the previous command is confirmed, without a fixed debounce delay. A transport-level ATT acknowledgement alone does not prove execution; use the exact RPC terminal result, retain uncertain-outcome handling and confirm the final saved state. During a drag, requiring complete `get` and `scene-state` readbacks after every intermediate value adds round trips and must be evaluated separately from final confirmation. Preserve group member isolation and do not replay a write with unknown outcome. The app owner is implementing and testing its side; no installed-phone latency improvement is claimed yet.

The S3 source candidate configures eight concurrent connections, while the classic ESP32 product configures two. These are active-link limits, not the six legacy stored-bond slots; open operation does not require a bond slot. Neither a large connection count nor automatic eviction is yet physically qualified. Never evict a peer with an in-flight command; if reclaim is implemented, define and verify idle detection, fairness, reconnect and radio admission before enabling it. The currently installed image and available recovery path must be verified before any update.
