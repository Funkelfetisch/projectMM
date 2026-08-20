# HELIO centered wall source and camera modes

## Goal

Make the virtual room projection read as light emitted through the infinity sphere instead of several unrelated point lights near its rim, and add predictable camera motion for both the main and scene previews.

## Design

- Project direct and reflected wall light from one virtual origin at the sphere center. LED colors, reflection attenuation, the 110-degree emission spread, wall distance, and rim occlusion still shape the result.
- Keep the existing sinusoidal top-to-bottom Tilt mode.
- Add a mutually exclusive Swivel mode: the camera travels around a circular path 45 degrees from the wall normal and keeps looking through the sphere center toward the wall.
- Auto-enable Tilt for visible scene previews. Their shared renderer and viewport scheduler continue to bound rendering cost.
- Expose Tilt and Swivel together in the main preview, floating preview, and advanced rendering controls.

## Verification

- Cover camera-mode persistence, exclusivity, renderer state, and centered projection with JavaScript tests.
- Inspect the main and scene previews in a browser at desktop and compact widths.
- Build the `helio1` firmware, upload it to `10.0.0.87`, and verify the served UI and controller state after reboot.

## Outcome

The centered projection, automatic card Tilt, and mutually exclusive main Tilt/Swivel modes ship in UI build `20260713j`. All JavaScript tests pass, the browser uses live device pixels for the large preview and viewport-scheduled simulations for cards, and the flashed controller remains connected at 34 FPS while the page is open.
