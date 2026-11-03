# Restore HELIO dashboard

## Intent

`helio1` opens a product dashboard with the sphere simulator, scene controls, playlists, setup, ambient-brightness status, and a global manual brightness slider. The MoonLight module editor remains reachable from the dashboard.

## Alternatives

1. Keep the generic module editor and add a brightness card. This is small, but omits the simulator and product workflow.
2. Restore the HELIO dashboard as a `helio1` UI view while retaining the current MoonLight editor. This reuses the established product interaction and keeps upstream UI behavior for every other firmware.
3. Replace the whole current UI with the former HELIO application. This restores the visual surface but discards current MoonLight behavior and creates a permanent merge burden.

Option 2 is the smallest complete product path. The dashboard-specific static assets are served only for HELIO, and the generic editor retains its current ownership of modules and controls.

## Steps

1. Restore the dashboard's static simulator, scene-preview, and orchestration assets and expose them through the embedded UI server.
2. Port the HELIO-only navigation, product views, and global brightness control onto the current application state and REST helpers.
3. Keep full state metadata on HTTP and compact value state on WebSocket so the dashboard does not reintroduce the controller memory regression.
4. Build the ESP32 image, run the desktop unit and scenario suites, and verify the dashboard and brightness control on the 222-light controller.
