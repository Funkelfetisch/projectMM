# SK6812 RGBW output migration

## Intent

HELIO 1 drives 222 SK6812 RGBW LEDs on GPIO 35. The hardware output uses RGBW byte order, the SK6812 300/600 ns timing profile, a linear response curve, and direct RGB preservation. The brightness service remains active. The bench consumes four bytes per LED, while its white emitters stay dark, so the shared RGB component is not moved into the white byte.

## Changes

1. Add an SK6812 RGBW timing profile to the RMT driver without moving existing timing indices.
2. Apply the fixed product timing after persisted generic timing controls load, so an earlier generic selection cannot change HELIO wiring at boot.
3. Seed HELIO 1 with RGBW, SK6812 RGBW, linear output, and direct RGB preservation. Existing persisted output controls continue to load.
4. Add unit coverage for the fixed timing, then build desktop and ESP32 separately before controlled per-channel device checks.
