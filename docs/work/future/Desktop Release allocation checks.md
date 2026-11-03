# Desktop Release allocation checks

An incidental GCC 13 Release test build on 27 October stops at `unit_MoonStatsModule.cpp` lines 330 and 373. The optimiser diagnoses `setName()` calls through potentially null allocations; the platform-backed module allocator is nullable. The affected fixtures allocate `MoonCloudModule` and `MoonStatsModule` without checking the pointers first.

The product acceptance matrix uses the existing `--tests --gcc --product helio1` Debug build, matching CI. Its result does not qualify the default Release target. Add explicit allocation assertions to these two fixtures, rerun the Release wrapper and inspect any further diagnostics before claiming Release acceptance. No compiler warning is disabled. The diagnostic logs are `preset-interaction-build-3.log` in the dated external firmware capsule.
