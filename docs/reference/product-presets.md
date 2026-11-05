# Product presets

HELIO and NEBULITE cards edit the native documents in `/.config/presets`. The Control module and HTTP use one store, with at most 64 presets. Opening a card reads its document and revision. The HTTP reply takes ownership of the file buffer and preserves its exact bytes and ETag. Editing saves that card without selecting a different animation. Selecting Play applies a saved version.

The [implementation plan](../work/present/Plan-20261026%20-%20product%20presets%20and%20usage.md) records build and device acceptance.

## Documents and revisions

Names contain 1–31 printable ASCII characters, excluding dot, slash and backslash. A document is a native state document. Optional `$origin` identifies its source scene/catalog; `$uiSchema` describes editable controls and their exact document paths. A global palette control has `path:["Drivers"]`; other controls use `effectPath`. These metadata fields grant no authority to change hardware.

| Request | Result |
|---|---|
| `GET /api/presets` | `items`, last-applied `active.effects` hint, `limit` |
| `GET /api/presets/<name>` | Native document and quoted SHA-256 `ETag` |
| `PUT /api/presets/<name>` | Conditional atomic save; `stored`, `created`, `name`, `etag`, `applied:false` |
| `DELETE /api/presets/<name>` | Conditional removal; `deleted`, `name` |
| `POST /api/presets/<name>/apply` | Conditional application; `applied`, `name`, `etag`, `changes`, `outputEtag` |
| `GET /api/preset-templates` | Source `catalog` and template `items` |
| `GET /api/preset-templates/<id>` | Complete factory document, without changing playback |

The index snapshots at most 64 names and streams one entry at a time in a close-delimited HTTP response. Each entry carries the content tag read with that file; the list has no atomic whole-index revision. A read or allocation failure after the headers closes an incomplete JSON document. Clients accept the complete document before reconciling cards, so a failed list does not remove retained records or drafts.

Create with `If-None-Match: *`. Overwrite, delete and apply require `If-Match: "<etag>"`. Apply sends `{"scope":"look"}`. An absent precondition returns 428; a stale version returns 412; invalid input returns 400; forbidden look content returns 403; storage failure returns 507. Storage and playback are separate operations. A successful PUT means the document reached storage; the state engine validates actual control values when it applies that document.

The HTTP server reserves its response slot and a 512-byte reply buffer before a mutation. A completed apply returns `applied:true` even if the subsequent output hash runs out of memory; that response includes `outputEtag:null` and a warning. Engine errors retain their error status and change report. A lost HTTP response still requires reading actual state before deciding what to do next.

The product client uses look scope: Effects plus the supported global palette fields. Driver hardware, layout, network and service configuration remain outside that scope. The generic interface can explicitly request full configuration scope. `apply=if-active` is unsupported: the last applied name alone does not establish current playback.

## Editing and playback

The product client retries a GET answered with HTTP 503 at most twice, after 150 and 300 ms. It reads each error body before waiting. Writes, network errors, other status codes and invalid successful responses are returned without retry. A failed background preview waits two seconds before loading again; explicit Controls and Play can read immediately.

Each card owns its name, document, ETag and pending draft. Debounced saves run serially; an old response cannot replace a newer draft. A 412 preserves the draft and stops automatic overwrite. Save a copy retains a conflicting or externally removed preset under another name.

Content-only saves notify file consumers and advance preset revisions. The Control module rebuilds its schema when a row is added, removed or its visible slot, roles, look or version marker changes. A tuning save retains the row identity and the current metadata stream.

A confirmed Play releases the selection queue before optional status observers finish. Runtime scene controls remain pending until a targeted Effects resource read completes; saved per-card controls remain editable. A newer native selection cancels further unsent steps and queued work from an older legacy scene request. Requests already transmitted retain their normal outcome and conditional-write semantics.

Play is an explicit request. After Play, an automatically saved tuning change can update its preview only with `If-Output-Match: "<outputEtag>"`. The server compares the actual writable Effects state and global palette before applying. Read-only telemetry is excluded. An intervening app/BLE change returns 412; the preset remains saved and the other playback continues. A null output hash requires another explicit Play before automatic preview resumes. These hashes describe configuration, not physical LED output.

Reset fetches a factory template and saves it with the preset's current file revision. Changes made while that template loads prevent the reset from replacing a newer draft. Delete preserves current playback and suppresses pending preview work for that card.

## Initial presets and startup

Factory initialization offers each factory scene once: it creates the missing files and lists the offered scene ids in `/.config/presets/.product-v1`. A deleted card stays deleted, and a scene the catalog gains later is offered on the next boot. A marker from an earlier build, which held a catalog revision, offers every missing look once. Existing files are never overwritten. Incomplete initialization leaves the marker unwritten and reports the failure.

An updated firmware may add animation templates without recreating deleted or customized cards. Use **Add animation** to create a preset from any current template. Musical-note labels stay visible in the template list; their suggested stored names use `Music -` because preset filenames currently accept printable ASCII only.

When an existing runtime effect type is unique, its exposed settings seed the initial file, including settings of disabled effects. The global palette is copied only for the active matching effect. Ambiguous instances use documented factory values. Template retrieval and Reset always use factory values.

Factory documents preserve the actual target layer name, its RGBW channels and its working-memory reserve. Competing code-wired scene content is explicitly disabled. The normal configuration persistence engine restores the last saved running state after startup; temporary RPC2 live patches retain their documented transient semantics.
