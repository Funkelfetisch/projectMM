# BLE simultaneous clients

The earlier ESP32 transport served one central at a time, sharing the handle, partial request, long-read snapshot, response, subscriptions and RPC2 session. Raising the NimBLE connection limit alone would have mixed requests and replies and allowed a second hello to invalidate the first client's session. The working-tree candidate now separates those records and configures two peers; the requirements below remain the qualification checklist rather than a claim that the device tests have passed.

To support simultaneous phones without compulsory bonds, the transport and RPC service must change together:

1. Keep a bounded per-connection record, identified by a handle plus a generation that changes on every reuse. Give each record its own incoming frame, response/read-blob snapshot, notifications and per-BAS subscription flags. Continue advertising while capacity remains. Releasing one record must not clear another or accepted scheduled work.
2. Carry that connection identity with each received request and targeted reply. A delayed reply to a disconnected generation must be discarded; it must never be delivered to a later connection that reused the handle. Backpressure must not let one client's incomplete or unconsumed request corrupt another.
3. Keep independent RPC2 challenge, high-water mark and cached reply per active client, with session identifiers unique across the complete service epoch. A hello, retry or exhaustion on one client must not reset another. Preserve the current explicit resume and pending/cancel semantics, including inspection of an old-session job by a new client. A service restart still changes the epoch.
4. Deliver state and battery notifications only to each actual subscriber. A long read must remain a stable snapshot for that connection while another client publishes or reads a different response. Existing public RPC2 fields and the open-GATT policy remain unchanged.
5. Qualify bounded allocation and failure handling on both S3 and classic ESP32 before advertising additional capacity. Test interleaved fragmented writes, simultaneous hello with equal IDs, retries, disconnect/reconnect and handle reuse, overlapping long reads, independent CCCDs, full capacity and recovery, and queued group completion. Follow host tests with two real centrals on the actual radio while Wi-Fi, LED output and the app are active.

The current source configures two simultaneous peers, but physical acceptance remains open. Capacity is a simultaneous radio/resource limit, not a limit on the number of phones allowed to use the product over time. Do not evict stored bonds as a way of managing it.

An isolated Linux probe is prepared for a future shared Mi 9 / Linux window.
It requires a time-bounded two-owner lease, verifies the system/BLE identity,
requests no pairing, and issues only fresh RPC2 hello and get operations with
per-fragment lease checks. Its unit tests cover lease refusal, fragmented
requests and clean disconnect with a fake transport. App and Linux time spans,
epochs and distinct sessions must be compared to establish actual overlap;
the probe alone cannot establish a second live client. No shared window or
two-client device result has yet been recorded for this probe.

An ESP32-S3 toolchain `sizeof` check on 2026-10-29 measured `BlePeerTable<1>` at 2120 bytes and `<2>` at 3712 bytes; `BleRpcPeers<1>` is 1160 bytes and `<2>` is 2280 bytes. Thus the two application-side objects together are 2712 bytes larger when both are allocated. The peripheral owns the table, while the RPC pool is allocated on demand. These are type sizes, not measured free heap, peak usage or proof of Wi-Fi/RMT/BLE coexistence; NimBLE controller/host allocations, fragmentation and simultaneous product workloads remain to be measured on hardware.

## Isolated HELIO radio checkpoint, 29 October 2026

The isolated two-peer S3 image SHA-256 `759d30f2c3dc8b218a4a6eb8491f3438d02e891de006cd113031ea58bbdce3e5` was installed once on the MAC-identified 222-pixel sphere. OTA receipt confirms the runtime build `Oct 29 2026 00:53:17`, restored settings and lighting off; the captured last panic was unchanged. With one Linux central, open RPC2 identity, fresh hello and correlated info response succeeded. One preceding attempt disconnected during service discovery. A separate initial assertion incorrectly expected the build timestamp in the BLE identity: that field actually reports `nogit`; the runtime timestamp was independently verified through HTTP. These observations do not prove stable reconnect or simultaneous clients. The app owner has the exclusive read-only window for two real phones; no multi-central acceptance is claimed pending explicit handback and evidence.

When one client is connected and the second slot is available, the candidate now continues to refresh the scan-response hint instead of suppressing all updates merely because a primary client exists. The full-capacity case waits until advertising restarts. The legacy NimBLE API documents scan-response updates while advertising as potentially busy, so the candidate stops and restarts advertising for a changed hint, with a one-second rate limit; connected links are not deliberately disconnected. The final ESP32-S3 application builds (SHA-256 `3812354e30733fe500ccfbb16365b3a04dcc011aaadc9cde58c6ab7e3351f650`); that image has not been installed. Live update timing, scan visibility and two-client radio operation remain unverified.

The desktop test of advertisement refresh passes six checks covering an unavailable slot, recovery after a failed start, unchanged hints, throttling and timer wrap. A wider name-filtered run reported 306 passing test cases and 27,586 assertions, but its container wrapper could not record the result because the parent Git directory was not mounted; the focused test was rerun successfully with the worktree's actual Git directory mounted. Neither run exercises the radio.

The same refresh candidate builds for the experimental classic ESP32 product target (application SHA-256 `895bd0e0eec47d999e84a95a2c6d82f3fdc37ea72f9dc74a178c6902adcba7c8`, 2,403,488 bytes). This only proves compilation and partition-size acceptance; classic-board wiring, coexistence and device output remain unqualified, and this image is not approved for installation.

The shared change also builds for the `nebulite-s3` product target (application SHA-256 `3e9b7a7425aaefd537b43ebd78b89c5b7f6fcf647b6c889f8ea78d9867712da5`, 2,935,072 bytes). This is build evidence only; wearable pins, battery measurement, optical output and real BLE coexistence remain unqualified.

A fresh GCC desktop run of the `BLE RPC2*` cases passes 22 cases/1,624 assertions, covering concurrent owners, reconnect, targeted group cancellation and deferred replies. This validates the host protocol implementation only, not two radio links or simultaneous physical light output.

During the active app-owned read-only window, Native144's first Mi 9 connection on the installed two-peer image produced 112 correlated RPC2 responses with a fresh nonce, no ATT errors and no lighting/configuration RPC. The app's evidence is `app144-two-peer-readonly-20261029/MI9-FIRST-RPC-RESULT.json` in the NEBULITE coordination directory. This is single-client evidence; the second client and reconnect checks are still in progress, and the app retains device ownership until explicit handback.

The subsequent app evidence `app144-two-peer-readonly-20261029/MI9-PARTIAL-TWO-SESSION-RESULT.json` records a normal Mi 9 disconnect and reconnect: 227/227 and 32/32 correlated read-only RPCs, distinct nonce/session values within the same boot epoch and no ATT errors. Its overall result explicitly says two simultaneous phones are not proven. The app window remains active until its handback.
