# HELIO room wall and camera tilt

## Goal

Place the HELIO sphere in a size-aware 3 x 3 m virtual room so its LEDs and internal reflections illuminate a physical wall. Add an optional smooth top-to-bottom camera tilt and keep the primary controls inside the preview.

## Model

- The LED count determines sphere diameter from the physical 144 LED/m strip pitch; 311 LEDs produce a sphere close to 70 cm.
- Wall distance is the gap between the rear sphere surface and a fixed 3 x 3 m wall plane.
- LED output has a 110 degree emission cone. Direct transmission is dominant and reflected images use the configured bounce attenuation.
- The opaque equatorial rim occludes projected light and casts a shadow on the wall.
- The wall stays in world space while the camera moves, so camera tilt changes perspective without moving the light pattern relative to the sphere and wall.

## Interaction

- Wall and Tilt are synchronized translucent toggles in the main preview corners and the floating preview toolbar.
- Wall distance is a persisted preview-rendering setting.
- Tilt traverses top to bottom and back with a smooth periodic curve. Its velocity reaches zero at the poles and equator to avoid visible direction or speed discontinuities.
- Main and floating previews render camera motion at 60 FPS while LED samples retain their device update cadence. Scene-card cameras remain fixed.

## Verification

1. Use a sparse moving source to verify broad wall diffusion, world-space perspective, and rim shadow movement.
2. Verify Wall and Tilt synchronization, wall-distance spread, fixed scene-card cameras, and reduced-motion behavior.
3. Run the JavaScript suite, build `helio1`, OTA-flash `10.0.0.87`, and inspect the device page in the browser.
