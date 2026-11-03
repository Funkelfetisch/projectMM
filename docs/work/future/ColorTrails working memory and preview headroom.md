# Color Trails working memory and preview headroom

The initial failure on the ESP32-S3 N8/no-PSRAM HELIO bench uses application SHA256
`54d1e6b507da0b314e03808d43269a03377872886d59046d3e57780693f1779c`
has a full 50 by 51 layer for Color Trails, but its physical output is black.
The layer allocation is 15,746 bytes including mapping. The effect reports
23,354 scratch bytes instead of the 38,654 bytes required by its two uint16
RGB planes, uint8 dither carry, and two int32 profiles. The difference is one
15,300-byte plane. Its null-buffer tick guard leaves the output black, while
the successful allocations remain held. This is independent of the corrected
layer-buffer reuse and reduced-raster mapping defects.

The same capture leaves approximately 19 KiB free heap; Particles leaves
approximately 29 KiB. Both lose the source-preview WebSocket during the
30-second recording. Particles emits visible light, but neither case qualifies
for dashboard/preview coexistence on this configuration. Six tested HELIO
factory scenes retain full 222-cell layers, changing preview frames and
approximately 53 to 55 KiB free heap. Original settings are restored after the
run; no additional firmware is installed for this finding.

The next correction needs an allocation-failure regression, an explicit status
and release of an incomplete scratch set, then a shared render-memory budget
or qualified lower-resolution rendering that preserves every physical output
position and enough headroom for the control transports. Replacing the effect
or changing physical product dimensions is not an established solution. A
successful host test is followed by native memory and physical-output checks.

Private evidence: `animations-abc-memory/run.json`, the two affected raw JPEGs,
30-second clips and source-preview error receipts under the bench evidence
root named in [the product bench](../../reference/product-bench-20261025.md).
These two cases are excluded from a released animation catalogue. No complete
animation ranking or all-animation freedom from unintended flashes is claimed.

## Implemented correction and remaining acceptance

The 26 October product candidate `f3ea858c` passes the scratch cleanup,
working-set admission and real API-creation regressions. Both effects now
produce changing native RGBW output for all 222 positions, at the explicitly
reported 25×25×1 raster, with cold mobile/desktop UI and encrypted BLE working.
The product send budget and admission headroom are separate. Exact artifacts,
counts and limits are in the product bench and its accepted-native summary.

New optical review, reduced-raster aesthetic judgement and all-animation
flash review remain open. Old failed recordings are not retroactively
qualified. A newly product-generated layer must inherit the product's render
headroom policy; current wiring sets it on the existing product layer.
