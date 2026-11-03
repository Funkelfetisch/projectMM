# HELIO sparse mapping under memory pressure

The UIi bench observation at 2026-10-27T18:35:41Z reports the active Layer status
`modifier mapping skipped — not enough memory`, with 10200 dynamic bytes. The
following existing WSP coordinate read returns all222 positions at stride1 and
epoch3. Header extents49/50/0 correspond to physical dimensions50×51×1; these are
observed coordinates, rather than dimensions inferred from the configured radius.

The unchanged `Layer::rebuildLUT` failure branch sets `lutSkipped_`, resets the
logical dimensions to the physical box and installs an identity LUT. A reduced
buffer would replace that status with the reduced dimensions. Together with the
10200-byte allocation and the active four-channel source, the observed state
therefore supports a50×51×1 logical raster with identity mapping: physical indexp
reads logical cellp for0–221. This is a source-derived runtime conclusion, not a
native dump of the logical LUT. The geometric RingLayout fold is a distinct
candidate and is not the active mapping in that observation.

Evidence resides in the external dated firmware capsule
`ble-rpc-20261026/deferred-helio-renderer-20261027`:
`effects-metadata-for-animation-handoff.json`, `native-coordinate-table-uii.json`,
its original binary and `website-current-rings-mapping-handoff-uii.json`.
Configuration and all25 preset-folder files remain unchanged after the workload.
That preservation does not establish preservation of an allocated runtime LUT.
No earlier matching mapping observation identifies when this fallback began.

The mapping fallback is outside the NamesOnly filesystem correction. A follow-up
must preserve the physical destinations of sparse layouts under memory pressure,
with bounded peak allocations and explicit raster/mapping status. Existing
buffer downscaling and folded-map rebuilding provide the relevant resampling
path; identity fallback changes the geometric source selection. Allocation-failure
coverage must exercise the counts, destination, CSR and frame-buffer stages,
including reduced rasters and gaps, before an identified native test. No specific
replacement algorithm or native mapping fix is qualified by this note.

The owner's current preferred Rings appearance is preserved as an explicitly
identified fallback reference. A mapping correction can change that appearance
while retaining the same saved controls. The simulator handoff separates the
observed wire geometry, source-derived current mapping and intended geometric
mapping. The cached CIE LUT for effective brightness161 is a timestamped host
reconstruction; automatic brightness remains live, so255 must not be assumed
from the saved manual brightness alone.

After the40-frame static source window on UIj, fresh read-only documents match
the latest saved Drivers/Effects/Services exactly, but Layer reports `50×51×1`
and 15746 dynamic bytes. The additional5546 bytes match the uint16 CSR offsets
and222 destinations. A fresh native coordinate table at epoch24 gives156
distinct cells, with physical74 reading cell1896; the host-native reconstruction
puts that one point at1946 and counts155 distinct cells. The native table takes
precedence for this timestamped source adapter. This is a successful sparse-map
allocation, with logical indices inferred from the unchanged flattening code,
not a direct export of native CSR memory. The earlier identity adapter is
historical. Repreparing the temporary source changed the allocation outcome
without changing the restored settings. Evidence is in
`names-only-preset-index-20261027/post-40-source-return-194505Z` and
`website-current-rings-mapping-post40-uij.json` beside it.


UIk83c5ddd4 returns to the identity fallback after its performance OTA and scoped
scene-test restoration. All25 native preset files and three writable documents
remain exact. Fresh Layer warning/dynamic10200B and a new stride1/epoch3 native
coordinate table corroborate the full50×51 raster with physical0..221 reading
source0..221. The geometric table still has156 distinct cells; it is separate
from this active fallback source adapter. The current handoff is
`ws-tree-coalescing-20261027/website-current-rings-mapping-handoff-uik.json`.
The allocation-dependent mapping issue remains open; this performance change
does not replace its algorithm or qualify a physical appearance.


## In-place CSR candidate, 28 October 2026

The source candidate builds counting-sort CSR directly in MappingLUT-owned
storage. It releases the prior table before allocating offsets, counts and
prefixes in those offsets, allocates exactly the accepted destinations and
scatters into the final storage. The offsets become write cursors during the
second deterministic walk and are restored afterwards. The temporary counts
and destination arrays and their second CSR copy are removed. A final paged
run allocates only its remaining entries. Offset loops use size_t through the
uint16 sentinel boundary.

Layer retries a refused table at a smaller raster with the original logical
coordinates and modifier chain. It no longer substitutes identity for sparse,
shuffled or gapped geometry. A completely refused mapping leaves an empty
nonidentity map and an explicit error. Drivers therefore retain the physical
composition path, which clears an available output buffer. This does not
promise that latched LEDs can be cleared if the physical output/DMA allocation
itself is unavailable. That hardware output condition needs its own evidence.

RandomMap prepares its permutation in modifyLogicalSize before either table
walk. Coordinate reads no longer retry a failed permutation allocation in the
middle of the two-pass build. A later prepare retries the allocation.

The first focused host run passes40 cases with14988 assertions. The final
candidate including RandomMap passes the full GCC product suite with3070 cases
and250758 assertions, plus49 focused ASAN cases with15835 assertions. Leak
detection is disabled by the canonical sanitizer wrapper. Both native product
images subsequently compile from immutable source archive `319faafabc4a7900`.
HELIO image `332587ba` is installed on the identified 222-pixel sphere before the
owner's night-off instruction. Its cached 50×51 raster and 15746 dynamic bytes
replace the older identity snapshot. A fresh pre-update observation also has
that full raster, so the update alone does not establish an identity-to-geometric
transition. The primary sphere remains off during the night; its optical and
fresh BLE-central acceptance are incomplete.

The same image is installed on the separate owner-authorized `10.0.0.87` controller,
MAC `34:CD:B0:37:4E:BC`. Its own 311-pixel/GPIO35/GRBW configuration, audio settings,
scene and network are preserved and six writable documents compare exactly
across one reboot. Read-only 390/1280 px dashboard checks pass without overflow,
page errors or writes. This sphere has no webcam; physical optical acceptance
and a BLE-central test remain open. Its evidence is in the separate
`flash-10-net-20261028` directory. Neither update qualifies every allocation branch
or intended Rings appearance.

The installed in-place CSR image retains the original flat-table admission gap:
its flat offsets and destinations do not independently enforce the Layer
working-memory reserve. Peak reduction does not establish that every admission
preserves the reserve. The separate source candidate below addresses the guard;
real transport coexistence remains part of native acceptance.
Evidence and the before/source copies reside in the external capsule
ble-rpc-20261026/sparse-mapping-20261028.


## Reserve admission candidate, 28 October 2026

A separate source candidate admits offsets and actual destinations against one
per-build payload budget and the currently reported heap, including known
internal headroom. Flat and paged allocations use the same reserve. A layer
passes its product-specific reserve; smaller requests retain the platform floor.
Existing offset and destination storage is reused, with held capacity accounted
separately from the current fill limit. Reused storage needs no new admission.
A growing destination allocation releases its old storage before admission.

Layer retains a coherent admitted raster when growth cannot fit and still
re-enumerates coordinates into the CSR. The API-created effect's unadmitted
scratch is released before the first mapping admission. The minimum render-buffer
fallback uses the same admission check: a fresh output without headroom reports
an error and holds no raster, while compatible admitted buffers remain reusable.
These changes replace unconditional reallocation of already admitted storage
and the unchecked minimum buffer; they do not skip live geometry updates. A desktop tracked-payload capacity fixture now credits actual
release and debits subsequent allocations, while existing static heap fixtures
keep their original semantics.

This is cooperative admission rather than an atomic reservation across other
threads. The host model excludes allocator metadata and unrelated process
allocations. Current host gates and reviews are recorded in the external
`mapping-reserve-20261028` capsule; native compilation, transport coexistence and
physical output acceptance remain separate. This candidate is not installed.


Revision3 passes the full GCC product suite: 3086 cases and 263605 assertions,
including 56 focused reserve, mapping, Layer and product-preset cases with 27841
assertions. The earlier compile failure is a nested doctest assertion in the new
test; it is corrected without relaxing the production checks. The subsequent
full-suite failure exposes an old fresh-output expectation that depends on the
unchecked allocation. Its replacement asserts an empty error state under the
product reserve and complete 64-cell RGBW recovery after pressure clears.
Separate admitted-scene reuse and physical-destination byte checks remain strict.
All failed logs are retained. Sanitizer and native gates follow separately.
