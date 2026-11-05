# Plan: snappy BLE

The product owner wants the app to feel snappy, and asked on 4 November 2026 for all three of the app session's proposals. On the Mi 9 with `ae3e7480`, tapping Rings shows its options after about 4.1 s: 13 RPC round trips at about 250 ms each. Connecting costs 6 or 7 more. Every round trip is a request write, an 8-byte "changed" notification, then a long read of up to 512 bytes, and every list pages at 512 bytes.

## Measured first

- **The live link.** `bleConnectionHint` asks only for a longer supervision timeout and keeps the central's interval. The BLE service's `link` diagnostics add the interval, latency and supervision timeout the link actually runs at, readable over HTTP, so the effect of each step below is measured rather than assumed.

## Wire (RPC3)

- **Identity** reports `"rpc":3`. The app keeps its RPC2 reader for devices not yet updated and picks by this field. The firmware carries RPC3 alone, since two reply paths would do one job (CLAUDE.md principle 3). An app older than this plan cannot talk to an RPC3 device: a [MIGRATING](../../reference/MIGRATING.md) entry.
- **Connection parameters.** When the central's interval is slower than 30 ms or has peripheral latency, the HELIO asks for 15 to 30 ms, latency 0 and a 4 s timeout, and keeps a faster central's interval. The floor is Apple's Accessory Design Guidelines (interval min at least 15 ms, max at least min + 15 ms): iOS refuses a 7.5 ms request. Android's high connection priority already runs 11.25 to 15 ms, so asking for 7.5 to 15 ms would gain nothing there.
- **Replies ride notifications.** The client enables notifications on State/result (`bee30701-…`) before `hello`, then writes requests to Command exactly as today. Each reply is its JSON envelope followed by one LF, sent to the requesting connection alone, in order, in notifications of at most ATT MTU − 3 bytes. The client appends payloads and splits at LF. Nothing is read. The 8-byte hint and the State read go. A reply the BLE stack has no buffers for is offered again every 10 ms until it goes out; a reply lost with its link is recovered by retrying the request, whose retained result is sent again.
- **Lists stream.** `scenes`, `scene-info`, `wifi`, `pixel-map`, `outputs` and `preview-frame` take one request. The device sends every page from `$offset` (default 0) to the end as consecutive replies with the request's `id`, each the same complete envelope a page has today, the last with `next:null`. The client sends no follow-up request and merges pages as today. A continuation repeats the first page's `revision` itself. A retried list request streams again from its `$offset`.
  - The app asked for one reply holding a scene's whole list. Pages streamed back to back cost the same single round trip without a 6 KB buffer per peer, which HELIO 1 cannot spare: no PSRAM, about 46 KB of internal heap free, six 256-byte NimBLE buffers. The page format the app already merges stays.

## Descriptors shipped with the app

- **`info`** adds `descriptors`, 16 lowercase hex digits: FNV-1a 64 over the catalog revision and every palette name in picker order, each followed by a NUL. Those are the only inputs a scene's descriptors have: the catalog revision already hashes the control code the descriptors come from (`gen_product_scenes.py`), labels and defaults come from the catalog, and palette options from the built-ins and the device's palette scripts, which a user may add.
- **`moondeck/helio/export_scene_descriptors.py --firmware <variant>`** writes `build/esp32-<variant>/scene-descriptors.json` from the desktop dispatcher, with no user palettes:

  ```json
  {"v":1,"firmware":"helio1","build":"ae3e7480","catalog":"e49001b8e695","descriptors":"0123456789abcdef",
   "scenes":{"solid":[{"name":"red","type":"uint8","default":255,"min":0,"max":255}],
             "ripple":[{"name":"palette","type":"palette","default":0,"min":0,"max":65,"options":[{"value":0,"label":"Rainbow"}]}]}}
  ```

  Each scene's list is what merging its `scene-info` pages gives, without `optionCount` and `optionOffset`. The app ships the files for the builds it knows. When a device's `descriptors` equals a shipped file's, the app needs no `scene-info` at all; otherwise it streams `scene-info` as above.

## Steps

1. Link diagnostics and the connection request, with the policy pinned by its tests.
2. Notification-carried replies: core chunking per peer (`BlePeerIo`), the ESP32 notify loop resumed by a 10 ms retry when the stack has no buffers, the desktop seam recording what is sent.
3. Streamed lists in `BleControlService`, one page per tick, after the previous page has gone.
4. The `descriptors` tag and the export script, checked against the dispatcher's own pages.
5. [product-ble.md](../../reference/product-ble.md) and MIGRATING describe RPC3; the helio1 image is built, and flashed on the product owner's go.

Removed: the hint notification, the State read and its long-read snapshot, and the client's follow-up page requests.
