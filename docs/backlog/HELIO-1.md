# HELIO 1 firmware

HELIO 1 is an ESP32-S3 RGBW sphere controller with 8 MB flash and no PSRAM. Its firmware key is `helio1`; generic MoonLight modules remain available. A clean installation starts with one ring of 222 LEDs and one RGBW RMT output on GPIO 35, matching the identified bench sphere's verified colour order. Persisted 311-LED installations retain their count and wiring. The ring has a configurable radius so both sizes keep their intended preview shape.

The microphone uses I²S bit clock GPIO 40, word select GPIO 38 and data GPIO 39, with no MCLK and a 22,050 Hz sample rate. Real local audio is the default source; simulated audio is an explicit user choice. A BH1750 at address `0x23` uses I²C bus 1, SCL GPIO 11 and SDA GPIO 26. Its automatic brightness target is transient; stored manual brightness remains the fallback and stored calibration remains authoritative.

The layer carries four logical channels. An effect's explicit W reaches the physical white LED in every white mode. With source W zero, `Min` synthesizes W from the common RGB component and `Accurate` moves that component from RGB to W. A warm-white RGB approximation affects browser preview only. A change of enabled effects starts the incoming effect on a clear buffer in the same render tick; an empty effect set outputs black.

The saved HELIO scene catalog remains native on the controller. Each proposed MoonLight substitute is checked against the corresponding scene's colors, motion, timing, audio gating and defaults before replacing a dedicated port. Browser scene cards illustrate those scenes; the main preview consumes the controller's actual source frame.

## Scene comparison in progress

The five existing native scene types use `RingsEffect`, `LavaLampEffect`, `DistortionWavesEffect`, `PlasmaEffect` and `MetaballsEffect`. Their render code is present in MoonLight; source diffs show documentation and control API changes. Ripple is the clean-install default on HELIO.

The other 19 scene types depend on the HELIO WLED preset engine. MoonLight has related effects such as `NoiseMeterEffect`, `GEQEffect` and `BeatRipplesEffect`, but their source behavior and controls are different. A matching name is insufficient to retire a HELIO scene. Preserve those saved types and their palette, timing and audio behavior before installing this firmware over an existing controller.

Build acceptance includes host unit and scenario tests, an `helio1` image built with the pinned ESP-IDF and a verified 8 MB partition table. Hardware acceptance separately includes boot/state readback, constant RGBW output, scene and control changes, persistence activity and at least 20 minutes of visual observation on each LED count. A clean source preview alone does not establish a clean RMT waveform or strip.
