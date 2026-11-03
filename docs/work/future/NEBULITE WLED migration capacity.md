# NEBULITE WLED migration capacity

The preserved v0.9 jacket and crop-top archives contain ESP32-S3 application
images (chip ID 9), 1,454,048 and 1,453,504 bytes respectively. The separate
`files.zip` contains settings and presets; a combined ZIP is not a full flash
image. The historical WLED source's 8 MB table documented by the app team has
two 2,097,152-byte OTA slots. That is a source-derived layout, not a readback
from every customer device.

Each of the 14 named presets in both preserved archives also contains an
opaque Base64 `r` field. Its decoded length is 4,650 bytes for each jacket
look and 1,350 bytes for each crop-top look. Those sizes are consistent with
50 × 31 × 3 and 50 × 9 × 3 respectively, matching the stored segment lengths,
but size alone does not establish frame encoding, frame rate, complete-cycle
coverage or whether a channel is a physical emitter. Preserve the original
archives; `inventory_wled_release.py` reports only decoded length and SHA-256,
not the field's content. The App owner has the same bounded offline lead.

The archived `AuraviaLTD/WLED` v0.9 `wled00/data/index.js` contains an
earlier base64 preview path that interprets recordings as three RGB bytes per
LED and advances every 100 ms. The same source then redefines that JavaScript
function for a JPEG/blob path, so the earlier declaration alone does not
prove what the shipped UI executed. All 28 named archived records decode into
50 complete RGB frames when explicitly dimensioned at 31 LEDs for the jacket
or 9 for the crop-top. Solid-record RGB values provide a useful consistency
check, but this does not establish physical output, capture time or the
recorded pixel-to-current-board mapping. `nebulite-recording.js` decodes
bounded, explicitly dimensioned recordings. A saved look with an explicitly
attached recording can show a labelled historical linear-strip preview only
while its Effects and Drivers documents match the captured binding; an edit
returns the card to its calculated preview. Preparing the attachment does not
write a preset and does not validate that an old recording matches a proposed
new scene. An offline-only preparer checks catalogue, native scene template,
control ranges, segment bounds and recording dimensions before attaching a
preview. Ten candidates, five from each archive, were prepared against actual
locally generated native templates under private capsule
`wled-look-drafts-20261029-0eabbb_m`; their optical and physical mapping
remain unqualified. The updated `wled-look-hashbound-20261029-zn_ob3te`
drafts additionally bind the decoded recording's SHA-256 to the archived
source, reject an oversized native document and fail on a mismatch. This hash
checks accidental mix-ups, not authenticity. When Web Crypto is unavailable on
plain HTTP, bounded local SHA-256 is used; its output matches Node's SHA-256
at block/padding boundaries and recorded-data sizes. The local NEBULITE UI now
offers review-file import for the matching configured product: it checks the
catalogue, reports supported and skipped entries, then conditionally creates
new names only after an explicit import action. It stops on an uncertain write
without replay. The archive itself is not unpacked on the device page, and the
UI has not yet been installed or browser-accepted on a wearable. A 390 px
headless Brave test against an isolated NEBULITE desktop instance reviewed a
real crop-top file, imported five temporary QA names after an explicit click,
and confirmed stable success feedback, no horizontal overflow, no page error,
unchanged prior preset ETags and unchanged Effects/Drivers documents. An
earlier attempt found a missing metadata fetch and disappearing success text;
both were fixed before the passing run. This is desktop/browser evidence, not
physical wearable or ESP32 memory acceptance. Prepared
review files are in private `wled-import-bundles-20261029-ku1dtltm`.
After the browser-driven fixes, the complete JS suite passes 582 tests and
the host Python suite passes 697 with five skipped; these runs do not cover a
real wearable, a real battery or the live BLE transport.
On an isolated desktop HTTP instance with a disposable filesystem, the real
web client imported five crop-top candidates under temporary QA names, read
each document and 50-frame recording back, refused a repeated import of those
names, and preserved all original preset ETags and the Effects/Drivers state.
The local process was stopped cleanly. This is not browser visual acceptance,
an ESP32 import, or a measurement of device heap during the transfer.
The native desktop `PresetStore` accepted and returned
all ten draft documents byte-for-byte in a disposable filesystem. A separate
HTTP test stored, indexed and applied a 6,200-character preview without
applying metadata as controls. These are host checks, not ESP32 heap,
filesystem, radio, optical or device-migration acceptance. Automatic WLED
firmware migration and device acceptance remain unimplemented.
Original archive bytes remain the authoritative backup.

The local 29 October `nebulite-s3` candidate with catalogue `5022ee54a0b8` is
2,948,496 bytes, SHA-256
`f929a79f7f6b49498158b04dd3406c44d4c1adb4a792049574201de344770e90`.
It exceeds either assumed WLED slot by 851,344 bytes. A normal WLED app-only
OTA cannot fit that layout; the chip ID alone does not authorize a target or
establish that an installed board uses it. `inventory_wled_release.py` can
compare an archived image and an explicit assumed slot size without printing
Wi-Fi credentials. A passing size check is not proof of live partition layout,
product wiring, acceptance by the updater, bootability or recovery.

The current S3 target's MoonBase layout instead has a 3 MiB application slot
and a separate factory recovery image. OTA does not replace the partition table.
Migrating a WLED device to that layout therefore requires a separately qualified
bootstrap or serial procedure, preserving settings and presets through a
verified backup and restoration. Do not offer an automatic WLED-to-MoonLight
update until board identity, live layout, recovery, asset identity, product
configuration and reboot have been established on the actual hardware.

The subsequent 28-scene source candidate, catalogue `85872f1e2e02`, builds as
2,954,416 bytes (SHA-256
`49fa6a32093bfa1dcd25dae3ae20294241a1a2626cb58560a196168b64d54ef5`),
857,264 bytes larger than that assumed historical 2 MiB slot. The build's own
3 MiB partition check leaves 191,312 bytes. Neither number establishes the
partition map of a physical wearable. Preserved image and selected build
inputs are in the private `nebulite-s3-washing-20261029-lsmgcauz` capsule;
they are not a complete source snapshot or a qualified migration package.

After adding the review-file importer, both S3 variants build again against
their configured 3 MiB application partitions. The NEBULITE image is 2,958,080
bytes, SHA-256 `f5ad9cb16ee498ee8ee506bc9f65c66b160de72e0c5f153d8e3c8c5dba2c4610`,
leaving 187,648 bytes in that partition. The HELIO image is 2,720,800 bytes,
SHA-256 `f9b97f19656622bab246615e1a1670d5f2f31b9601a78461b2c4c203669e6489`.
The files and selected source inputs are in private capsule
`dual-s3-import-20261029-icg83x8h`. Neither build has been installed, and
the selected-source capsule is not a complete reproducible source snapshot.

After the importer review and status fixes, fresh local builds also pass for
both wearable targets. The S3 app is 2,958,256 bytes (SHA-256
`252368d3b9a258f1d000df61beaff9fd60964542ef3f730e1c74de6146bba330`),
leaving 187,472 bytes in its configured 3 MiB app slot. The classic ESP32
app is 2,422,992 bytes (SHA-256
`eb953cc04ea6f4d63873d485a2c289406b22b5f7e90d424274343f7062576a61`),
leaving 132,912 bytes in its configured 0x270000-byte app slot. Its separate
MoonBase image also builds within the 0xe0000-byte factory slot. ESP-IDF warns
that the full app does not fit the factory slot; that slot is for MoonBase,
not the full app. The generic IDF flash command printed during the build puts
the full app at the factory offset and must not be used for this layout; the
MoonDeck flash wrapper supplies the explicit MoonBase/app offsets. Neither
build has been installed or tested on a physical wearable, and neither proves
that an existing WLED device has this partition map or a safe migration path.

The source candidate now also contains the archived Sine look (FX 108,
palette 1), exposed through the generated scene catalog and review-only import.
Its RGB browser preview matches six actual native desktop frames (31 pixels
each), including a held intermediate frame and a local random-palette change;
the JavaScript suite passes 585 tests, and the focused native segment test
passes 99 assertions. This compares the two new implementations, not their
light against the archived WLED device. The effect uses an independent random
seed, palette clock and effect elapsed clock; WLED's global palette, uptime,
missed frames and physical output still require separate comparison. The
earlier device images predate this candidate.

The archived FX.cpp Git blob `04a77701f20b0c5e68e8e1458aa06aec8cc32130` is
also compiled with instrumented pixel and palette callbacks by
`moondeck/helio/check_wled_sine_motion.py`. Its unchanged Sine function and
the new motion/index helper agree for 573,440 pixel evaluations across all
256 intensities, selected speeds, uptime wrap cases and 64 pixel positions.
The harness supplies FastLED 3.6 fixed-scale cubic wave math; it does not
compare palette generation, RGBW blending, scheduling or physical LEDs.

The archived ESP32 fast-path `colorTools.hpp` blob
`c47f1934684fde1d22e7e5801265d90db009f3ef` is compiled unchanged by
`moondeck/helio/check_wled_sine_blend.py`: all 16,777,216 combinations of
8-bit source, target and blend amount agree for RGBW channel blending. The
extracted helper preserves behavior but changes the generated catalog revision
to `3c0db6f2d929`; all 29 scene pages and 1,614 metadata pages pass the app
decoder check again. New private review bundles for this revision are under
`wled-sine-review-20261029-ohi0tymb`. The earlier build and prior review bundle
retain their own `cc21e9515f98` provenance and are not relabeled.

The source candidate adds palette 3 of WLED-MM FX 65 for the archived
`Barbie & Ken` look. Its original two RGB colors, speed and jacket/crop-top
segment bounds are recognized for review-only import. Six 31-pixel native
desktop frames match the browser preview. The catalog `3a47c291bd4e` now has
30 scenes; all pages and 1,624 metadata pages pass the app decoder, the
JavaScript suite passes 587 cases, and the Python suite passes 697 with five
skips. This is source/host evidence, not a physical comparison, WLED gamma or
transition equivalence, device installation, or an integrated app preview.
The unchanged archived `mode_palette` function is additionally compared with
the actual desktop effect through an instrumented palette callback by
`moondeck/helio/check_wled_palette_effect.py`; 1,600 RGBW pixels agree across
both archived segment lengths, selected speeds, stationary/moving wrap and
uptime rollover. Palette interpolation is shared in this test, so it is not
an independent check of that helper. Actual archive-derived review documents
for both products were also stored and read back on an isolated desktop
controller, retaining both RGB colors, speed and 31/9-light segments. The
review bundles are in private `wled-palette-review-20261029-yrsmw8te`.

Fresh private review bundles for both archives use catalog `cc21e9515f98` in
`wled-sine-review-20261029-4h66ni8o`. A factory preset already named Sine or
Washing Machine no longer silently blocks the historical look: review proposes
a distinct ` (WLED)` name, and conditional creation still refuses an existing
name. On an isolated desktop controller, the actual importer prepared both
Sine documents from the archived recordings; test copies were stored and read
back with the original 31- and 9-pixel segment bounds, speed and preview
length. The 585 JavaScript tests pass. No physical configuration or preset was
read or changed by this check. The source importer change postdates the last
wearable builds and must be rebuilt before device testing.

After the Sine and reviewed-import changes, all three product targets rebuild
with catalog `cc21e9515f98`: HELIO-S3 2,724,000 bytes, NEBULITE-S3 2,961,296
bytes, and classic ESP32 2,425,232 bytes. Their configured application slots
leave 421,728, 184,432 and 130,672 bytes respectively. Exact build outputs are
hash-verified in `sine-candidate-20261029-bw5f5twr`; the capsule is not a
complete source snapshot. Build success does not establish physical board
identity, partitioning, update recovery, battery calibration or optical output.
