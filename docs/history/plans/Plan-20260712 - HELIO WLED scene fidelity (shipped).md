# HELIO WLED scene fidelity

## Goal

Make the HELIO experience reproduce the saved `helio1` WLED scenes through native ProjectMM effect modules, expose the saved static and audio-reactive playlists on the HELIO dashboard, and keep the JavaScript scene previews visually aligned with the C++ output driving the LEDs.

## Reference

The authority is `usermods/NEBULITE/configs/helio1/presets.json` in the local WLED checkout. Its 19 scene presets define effect IDs, colors, palettes, speed, intensity, custom controls, segment options, and physical order. Its two playlist entries define scene order, duration, transition, and repeat behavior.

## Work

1. Record the saved preset and playlist values in one HELIO scene manifest and test that every entry maps to a registered dedicated scene module.
2. Audit each C++ scene against the corresponding WLED algorithm and saved controls. Port the recognizable behavior into ProjectMM's contiguous light-buffer model and add deterministic shape tests for regressions.
3. Route every HELIO card to its dedicated module. Render the two playlist controls directly on the HELIO dashboard while preserving immediate manual override.
4. Align each JavaScript preview with the C++ algorithm, timing, palette, colors, white channel, and current controls. Add cross-runtime reference checks for representative frames.
5. Verify scene switching, playlist stepping, live-pixel rendering, and inactive card previews in the browser and on the physical HELIO sphere. Build `helio1` and upload it to `10.0.0.87`.

## Constraints

- Audio scenes consume real audio and stay black below the input gate; previews use an explicit representative audio frame rather than feeding synthetic audio into firmware.
- Manual scene selection stops playlist playback before applying the selected scene.
- The live main preview remains sourced from C++ pixel frames; inactive cards use the deterministic JavaScript counterpart.
- The existing clear-glass, warm-white, radial-glow optics remain unchanged unless physical comparison shows a scene-independent mismatch.
