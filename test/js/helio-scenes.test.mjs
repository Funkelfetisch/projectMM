import { test } from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";

import {
    HELIO_FACTORY_SCENES,
    HELIO_SCENE_PLAYLISTS,
    HELIO_SIZE_PRESETS,
    helioLedCountForDiameterCm,
    helioPlaylistUsesAudio,
    helioPlaylistFirstScene,
    helioPreferredActiveEffect,
    helioSceneForPresetIndex,
    helioSceneUsesAudio,
    isHelioPortEffectType,
    isNativeHelioSceneType,
} from "../../src/ui/helio-scenes.js";
import { helioPresetPreviewColor } from "../../src/ui/helio-preview.js";

test("HELIO size presets match product diameters", () => {
    assert.deepEqual(
        HELIO_SIZE_PRESETS.map(preset => [preset.label, preset.count]),
        [
            ["30 cm", 131],
            ["50 cm", 222],
            ["70 cm", 311],
            ["100 cm", 446],
            ["150 cm", 671],
            ["200 cm", 895],
        ],
    );
    assert.equal(helioLedCountForDiameterCm(50), 222);
    assert.equal(helioLedCountForDiameterCm(70), 311);
});

test("HELIO scene catalog has one scene per source preset index", () => {
    const indexes = HELIO_FACTORY_SCENES.map(scene => scene.presetIndex).sort((a, b) => a - b);

    assert.deepEqual(indexes, Array.from({ length: 23 }, (_value, i) => i));
});

test("HELIO scene array index matches the preset index", () => {
    HELIO_FACTORY_SCENES.forEach((scene, index) => {
        assert.equal(scene.presetIndex, index, scene.id);
    });
});

test("HELIO playlists only reference catalog scenes", () => {
    for (const playlist of HELIO_SCENE_PLAYLISTS) {
        assert.ok(playlist.order.length > 0, `${playlist.id} playlist is empty`);
        for (const presetIndex of playlist.order) {
            assert.ok(helioSceneForPresetIndex(presetIndex), `${playlist.id} references missing preset ${presetIndex}`);
        }
    }
});

test("HELIO catalog routes migrated scenes through ProjectMM modules", () => {
    const nativeByPreset = new Map([
        [0, "HelioCandleMultiEffect"],
        [1, "HelioColorTwinklesEffect"],
        [2, "HelioFreqMapAmberEffect"],
        [3, "HelioLakeEffect"],
        [4, "HelioLoadingEffect"],
        [5, "HelioPinkPaletteEffect"],
        [6, "HelioPhasedEffect"],
        [7, "RingsEffect"],
        [8, "HelioRunningEffect"],
        [9, "HelioStarburstEffect"],
        [10, "HelioGravCenterEffect"],
        [11, "HelioJugglesEffect"],
        [12, "HelioMultiCometEffect"],
        [13, "HelioNoiseMeterEffect"],
        [14, "HelioPlasmoidEffect"],
        [15, "HelioFreqMapWideEffect"],
        [16, "HelioGEQEffect"],
        [17, "HelioGravFreqEffect"],
        [18, "HelioFreqWaveEffect"],
        [19, "LavaLampEffect"],
        [20, "DistortionWavesEffect"],
        [21, "PlasmaEffect"],
        [22, "MetaballsEffect"],
    ]);

    for (const [presetIndex, type] of nativeByPreset) {
        const scene = helioSceneForPresetIndex(presetIndex);
        assert.equal(scene.type, type);
        assert.equal(isNativeHelioSceneType(scene.type), true);
        assert.equal(isHelioPortEffectType(scene.type), false);
    }
});

test("HELIO scene cards all route through native ProjectMM effects", () => {
    for (const scene of HELIO_FACTORY_SCENES) {
        assert.equal(isNativeHelioSceneType(scene.type), true, scene.id);
        assert.equal(isHelioPortEffectType(scene.type), false, scene.id);
    }
});

test("HELIO Lake and Chase previews pin native byte-wave timing", () => {
    const palette = [[0, 0, 0], [255, 64, 0], [255, 200, 40]];

    assert.deepEqual(helioPresetPreviewColor(3, {}, palette, 2.345, 0.317, 0.5), [255, 211, 75]);
    assert.deepEqual(helioPresetPreviewColor(8, {}, palette, 2.345, 0.317, 0.5), [237, 59, 0]);
});

test("HELIO active effect selection prefers native scenes over stale compatibility effects", () => {
    assert.equal(helioPreferredActiveEffect([
        { name: "Pixelblaze", type: "PixelblazeCompatEffect", enabled: true },
        { name: "HELIO Ripple", type: "RingsEffect", enabled: true },
    ]).type, "RingsEffect");

    assert.equal(helioPreferredActiveEffect([
        { name: "HelioPresets", type: "HelioWledPresetsEffect", enabled: true },
        { name: "HELIO Ripple", type: "RingsEffect", enabled: false },
    ]).type, "HelioWledPresetsEffect");

    assert.equal(helioPreferredActiveEffect([
        { name: "HelioPresets", type: "HelioWledPresetsEffect", enabled: false },
        { name: "HELIO Ripple", type: "RingsEffect", enabled: false },
    ]), null);
});

test("HELIO scene manifest preserves saved WLED IDs and maps palettes by name", () => {
    const migratedScenes = HELIO_FACTORY_SCENES.slice(0, 19);
    assert.deepEqual(migratedScenes.map(scene => scene.source.preset), [
        1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
    ]);
    assert.deepEqual(migratedScenes.map(scene => scene.source.effect), [
        102, 74, 155, 75, 47, 65, 105, 79, 15, 192, 156, 130, 191, 136, 133, 155, 139, 158, 137,
    ]);
    assert.deepEqual(migratedScenes.map(scene => scene.source.palette), [
        0, 28, 11, 0, 18, 19, 45, 0, 73, 57, 11, 64, 64, 64, 28, 63, 11, 1, 1,
    ]);
    assert.deepEqual(migratedScenes.map(scene => scene.palette), [
        0, 28, 0, 0, 12, 50, 53, 0, 36, 21, 0, 56, 56, 56, 28, 14, 0, 36, 0,
    ]);
});

test("HELIO scene cards do not expose preset-player controls", () => {
    const presetPlayerControls = new Set(["idleBeat", "sound simulation", "audio map", "autoPlay", "playlist", "preset"]);

    for (const scene of HELIO_FACTORY_SCENES) {
        for (const controlName of Object.keys(scene.controls || {})) {
            assert.equal(presetPlayerControls.has(controlName), false, `${scene.id}.${controlName}`);
        }
    }
});

test("HELIO scene previews use live palette only for the active card", async () => {
    const appSource = await readFile(new URL("../../src/ui/app.js", import.meta.url), "utf8");

    assert.match(appSource, /function helioPreviewPaletteForScene/);
    assert.match(appSource, /paletteByIndex\(scene\.palette, fallback\)/);
    assert.match(appSource, /const pal = usesActiveControls \? paletteFromState\(\) : helioPreviewPaletteForScene\(mode\);/);
});

test("HELIO dashboard replaces raw color channels with RGBW pickers", async () => {
    const appSource = await readFile(new URL("../../src/ui/app.js", import.meta.url), "utf8");
    const styleSource = await readFile(new URL("../../src/ui/style.css", import.meta.url), "utf8");
    const nativeStart = appSource.indexOf("function helioNativeControlRows");
    const nativeEnd = appSource.indexOf("function helioHasDirectColor", nativeStart);
    const colorStart = appSource.indexOf("function helioColorRow");
    const colorEnd = appSource.indexOf("function helioButtonRow", colorStart);
    const nativeRows = appSource.slice(nativeStart, nativeEnd);
    const colorRow = appSource.slice(colorStart, colorEnd);

    assert.ok(nativeStart >= 0 && nativeEnd > nativeStart);
    assert.ok(colorStart >= 0 && colorEnd > colorStart);
    assert.match(nativeRows, /HELIO_COLOR_CONTROL_SPECS\.find/);
    assert.match(nativeRows, /rows\.push\(helioColorRow/);
    assert.match(nativeRows, /HELIO_COLOR_CHANNEL_NAMES\.has\(ctrl\.name\)/);
    assert.match(colorRow, /color\.type = "color";/);
    assert.match(colorRow, /const values = hasWhite \? rgbToRgbw\(picked\) : picked;/);
    assert.doesNotMatch(colorRow, /type = "range"/);
    assert.match(appSource, /const usesGlobalPalette = !helioHasDirectColor\(activeEffect\) && !helioControl\(activeEffect, "palette"\);/);
    assert.match(appSource, /usesGlobalPalette \? paletteRow : null/);
    assert.doesNotMatch(appSource, /helio-solid-row/);
    assert.doesNotMatch(styleSource, /\.helio-color-row input\[type="range"\]/);
});

test("HELIO playlist first-scene lookup follows playlist values", () => {
    assert.equal(helioPlaylistFirstScene(0).id, "ripple");
    assert.equal(helioPlaylistFirstScene(1).id, "geq");
    assert.equal(helioPlaylistFirstScene(2).id, "ripple");
});

test("HELIO playlists preserve saved WLED timing and looping", () => {
    assert.deepEqual(HELIO_SCENE_PLAYLISTS.map(playlist => ({
        sourcePreset: playlist.sourcePreset,
        duration: playlist.duration,
        transition: playlist.transition,
        repeat: playlist.repeat,
        order: playlist.order,
    })), [
        { sourcePreset: 10, duration: 10, transition: 0.7, repeat: true, order: [7, 3, 6, 8, 0, 4, 1, 5] },
        { sourcePreset: 11, duration: 30, transition: 2, repeat: true, order: [16, 17, 13, 15, 18, 11, 10, 12, 14, 9] },
        { sourcePreset: undefined, duration: 18, transition: 1.2, repeat: true, order: [7, 19, 20, 21, 22] },
    ]);
});

test("HELIO scene catalog marks audio-only choices", () => {
    const staticPlaylist = HELIO_SCENE_PLAYLISTS.find(playlist => playlist.id === "static");
    const audioPlaylist = HELIO_SCENE_PLAYLISTS.find(playlist => playlist.id === "audio");
    const spherePlaylist = HELIO_SCENE_PLAYLISTS.find(playlist => playlist.id === "sphere");

    assert.equal(helioSceneUsesAudio(helioSceneForPresetIndex(8)), false);
    assert.equal(helioSceneUsesAudio(helioSceneForPresetIndex(9)), true);
    assert.equal(helioPlaylistUsesAudio(staticPlaylist), false);
    assert.equal(helioPlaylistUsesAudio(audioPlaylist), true);
    assert.equal(helioSceneUsesAudio(helioSceneForPresetIndex(19)), false);
    assert.equal(helioPlaylistUsesAudio(spherePlaylist), false);
});

test("HELIO Sphere Optics uses distinct native 2D fields", () => {
    const sphere = HELIO_SCENE_PLAYLISTS.find(playlist => playlist.id === "sphere");
    assert.deepEqual(sphere.order.map(index => helioSceneForPresetIndex(index).type), [
        "RingsEffect",
        "LavaLampEffect",
        "DistortionWavesEffect",
        "PlasmaEffect",
        "MetaballsEffect",
    ]);
    assert.deepEqual(helioSceneForPresetIndex(22).controls, {
        bpm: 10,
        radius: 20,
        count: 2,
        hue_shift: 24,
    });
});

test("HELIO scene card copy stays product-facing", () => {
    for (const scene of HELIO_FACTORY_SCENES) {
        assert.doesNotMatch(`${scene.label} ${scene.meta}`, /\b(native|wled|source effect|glass)\b/i, scene.id);
        assert.equal(Object.hasOwn(scene, "sourceId"), false, scene.id);
    }
});

test("HELIO UI copy avoids renderer names", async () => {
    const appSource = await readFile(new URL("../../src/ui/app.js", import.meta.url), "utf8");

    assert.doesNotMatch(appSource, /textContent\s*=\s*"HelioTrace"/);
    assert.doesNotMatch(appSource, /aria-label",\s*"[^"]*HelioTrace/);
    assert.doesNotMatch(appSource, /title\s*=\s*"Open HelioTrace/);
});

test("HELIO favicon uses the embedded signet asset", async () => {
    const appSource = await readFile(new URL("../../src/ui/app.js", import.meta.url), "utf8");
    const indexSource = await readFile(new URL("../../src/ui/index.html", import.meta.url), "utf8");
    const embedSource = await readFile(new URL("../../src/ui/embed_ui.cmake", import.meta.url), "utf8");
    const httpSource = await readFile(new URL("../../src/core/HttpServerModule.cpp", import.meta.url), "utf8");

    assert.match(indexSource, /<link id="favicon" rel="icon">/);
    assert.match(indexSource, /<img id="brand-logo" hidden alt="">[\s\S]*<span class="brand"><\/span>/);
    assert.match(appSource, /function applyFavicon\(helio\) \{[\s\S]*favicon\.type = helio \? "image\/svg\+xml" : "image\/png";[\s\S]*favicon\.href = helio \? "\/helio-favicon\.svg" : "\/moonlight-logo\.png";[\s\S]*\}/);
    assert.match(appSource, /applyFavicon\(helio\);/);
    assert.match(appSource, /logo\.hidden = false;[\s\S]*logo\.src = helio/);
    assert.match(httpSource, /"\/helio-favicon\.svg"[\s\S]*serveFile\(conn, "helio-favicon\.svg", "image\/svg\+xml"\)/);
    assert.match(httpSource, /"helio-favicon\.svg"[\s\S]*ui::helioFaviconSvg[\s\S]*ui::helioFaviconSvgLen/);
    assert.match(embedSource, /gzip_file_hex\("helio-favicon\.svg" HELIO_FAVICON_SVG\)/);
    assert.match(embedSource, /constexpr uint8_t helioFaviconSvg\[\]/);
});

test("HELIO advanced labels stay product-facing", async () => {
    const appSource = await readFile(new URL("../../src/ui/app.js", import.meta.url), "utf8");

    assert.match(appSource, /"source defaults": "Reset scene"/);
    assert.match(appSource, /"sound simulation": "Demo beat"/);
    assert.match(appSource, /"audio map": "Audio shape"/);
    assert.match(appSource, /"hue_shift": "Hue shift"/);
    assert.match(appSource, /"fadeOut": "Trail fade"/);
    assert.match(appSource, /"smoothBars": "Smooth bars"/);
    assert.match(appSource, /"colorBars": "Color bars"/);
    assert.match(appSource, /"star_fill_ratio": "Sparkle density"/);
    assert.match(appSource, /"usePalette": "Palette colors"/);
    assert.match(appSource, /"custom 1": "Fine tune 1"/);
    assert.match(appSource, /"option 1": "Extra mode 1"/);
    assert.match(appSource, /helioToggleRow\(activeEffect, "idleBeat", "Demo beat"/);
    assert.match(appSource, /const sceneControls = helioSceneControls\(helioControlSceneForEffect\(activeEffect\)\);/);
    assert.match(appSource, /sceneRow\("custom 1", \(\) => helioSliderRow\(activeEffect, "custom 1"\)\)/);
    assert.match(appSource, /sceneRow\("sound simulation", \(\) => helioSliderRow\(activeEffect, "sound simulation"\)\)/);
    assert.match(appSource, /sceneRow\("secondary red", \(\) => helioColorRow\(activeEffect, "Accent"/);
    assert.doesNotMatch(appSource, /textContent\s*=\s*"Source defaults"/);
    assert.doesNotMatch(appSource, /"Synthetic beat"/);
    assert.doesNotMatch(appSource, /helioControlGroup\("Source"/);
});

test("HELIO navigation keeps raw modules directly reachable in a hideable sidebar", async () => {
    const appSource = await readFile(new URL("../../src/ui/app.js", import.meta.url), "utf8");
    const styleSource = await readFile(new URL("../../src/ui/style.css", import.meta.url), "utf8");

    assert.doesNotMatch(appSource, /HELIO_MODULES_VIEW/);
    assert.doesNotMatch(appSource, /renderHelioAdvancedModules/);
    assert.doesNotMatch(appSource, /modules\.textContent = "Advanced";/);
    assert.match(appSource, /if \(helioDevice\) \{[\s\S]*home\.textContent = "HELIO";[\s\S]*playlists\.textContent = "Playlists";[\s\S]*\}[\s\S]*for \(const mod of state\.modules\)/);
    assert.match(styleSource, /body\.helio-device-mode #nav-toggle \{/);
    assert.match(styleSource, /body\.helio-device-mode:not\(\.nav-open\) #nav \{[\s\S]*display: none;/);
});

test("HELIO header brand opens the product dashboard", async () => {
    const appSource = await readFile(new URL("../../src/ui/app.js", import.meta.url), "utf8");
    const styleSource = await readFile(new URL("../../src/ui/style.css", import.meta.url), "utf8");

    assert.match(appSource, /function openBrandHome\(\) \{[\s\S]*selectModule\(HELIO_VIEW\);/);
    assert.match(appSource, /function setupBrandNavigation\(\) \{[\s\S]*document\.getElementById\("brand-logo"\)/);
    assert.match(appSource, /setupBrandNavigation\(\);/);
    assert.match(styleSource, /\.brand-home-link \{[\s\S]*cursor: pointer;/);
});

test("HELIO dashboard omits redundant title and status metrics", async () => {
    const appSource = await readFile(new URL("../../src/ui/app.js", import.meta.url), "utf8");
    const styleSource = await readFile(new URL("../../src/ui/style.css", import.meta.url), "utf8");

    assert.doesNotMatch(appSource, /function helioMetric/);
    assert.doesNotMatch(appSource, /title\.textContent = "HELIO"/);
    assert.doesNotMatch(appSource, /helioMetric\("LEDs"/);
    assert.doesNotMatch(appSource, /metrics\.className = "helio-metrics"/);
    assert.doesNotMatch(styleSource, /\.helio-metrics\b/);
    assert.doesNotMatch(styleSource, /\.helio-metric\b/);
    assert.doesNotMatch(styleSource, /\.helio-stage h1/);
});

test("HELIO dashboard groups full scene previews into two-column playlists", async () => {
    const appSource = await readFile(new URL("../../src/ui/app.js", import.meta.url), "utf8");
    const styleSource = await readFile(new URL("../../src/ui/style.css", import.meta.url), "utf8");

    assert.match(appSource, /panels\.append\(renderHelioControlPanel\(\)\);/);
    assert.match(appSource, /renderHelioPlaylistsView\(wrap, \{ dashboard: true \}\);/);
    assert.doesNotMatch(appSource, /renderHelioScenePanel/);
    assert.match(appSource, /for \(const playlist of HELIO_SCENE_PLAYLISTS\) \{[\s\S]*playlist\.order\.forEach\(presetIndex => \{[\s\S]*renderHelioSceneCard\(preset, status, active, audioReady\)/);
    assert.doesNotMatch(appSource, /helio-playlist-preview/);
    assert.doesNotMatch(appSource, /helio-playlist-step/);
    assert.match(appSource, /wrap\.appendChild\(renderHelioSetupMenu\(\)\);/);
    assert.match(appSource, /function renderHelioSetupMenu\(\)/);
    assert.match(appSource, /renderHelioSetupPanel\(\{ forceExpanded: true, embedded: true \}\)/);
    assert.match(styleSource, /\.helio-playlist-grid \{[\s\S]*grid-template-columns: minmax\(0, 1fr\);/);
    assert.match(styleSource, /\.helio-playlist-sequence \{[\s\S]*grid-template-columns: repeat\(2, minmax\(0, 1fr\)\);/);
    assert.match(styleSource, /\.helio-dashboard-menu > summary \{[\s\S]*grid-template-columns: minmax\(0, 1fr\) auto auto;/);
    assert.match(styleSource, /body\[data-brand="helio"\] \.helio-dashboard-menu/);
    assert.match(styleSource, /@media \(max-width: 720px\) \{[\s\S]*\.helio-playlist-sequence \{[\s\S]*grid-template-columns: 1fr;/);
});

test("HELIO ambient settings explain the two-point brightness curve", async () => {
    const appSource = await readFile(new URL("../../src/ui/app.js", import.meta.url), "utf8");
    const styleSource = await readFile(new URL("../../src/ui/style.css", import.meta.url), "utf8");

    assert.match(appSource, /Choose the LED brightness for a dark and a bright room/);
    assert.match(appSource, /\["darkLux", \["Dark reference \(lux\)"/);
    assert.match(appSource, /\["brightBrightness", \["Brightness in bright room"/);
    assert.match(appSource, /\["hysteresisLux", \["Minimum change \(lux\)"/);
    assert.match(appSource, /help\.className = "helio-ambient-control-description"/);
    assert.match(styleSource, /\.helio-ambient-control-description \{[\s\S]*line-height: 1\.35;/);
    assert.match(styleSource, /@media \(max-width: 520px\) \{[\s\S]*\.helio-ambient-settings \.control-row \{[\s\S]*grid-template-columns: 1fr;/);
});

test("HELIO ambient settings stay live in product mode", async () => {
    const appSource = await readFile(new URL("../../src/ui/app.js", import.meta.url), "utf8");
    const homeStart = appSource.indexOf("function updateHelioHome");
    const homeEnd = appSource.indexOf("function startHelioModePreviews", homeStart);
    const homeBody = appSource.slice(homeStart, homeEnd);

    assert.ok(homeStart >= 0 && homeEnd > homeStart);
    assert.match(homeBody, /const ambient = helioAmbientLight\(\);[\s\S]*if \(ambient\) updateModuleControls\(ambient\);/);
});

test("HELIO disables manual brightness while ambient control overrides it", async () => {
    const appSource = await readFile(new URL("../../src/ui/app.js", import.meta.url), "utf8");
    const styleSource = await readFile(new URL("../../src/ui/style.css", import.meta.url), "utf8");

    assert.match(appSource, /function helioAmbientBrightnessOverride\(\)/);
    assert.match(appSource, /ambient\.enabled !== false[\s\S]*controlValue\(ambient, "automatic", false\)[\s\S]*Number\.isFinite\(target\)/);
    assert.match(appSource, /bright\.disabled = ambientOverride\.active/);
    assert.match(appSource, /ambientOverride\.active \? "Manual fallback" : "Brightness"/);
    assert.match(appSource, /This slider stores the manual fallback and becomes available when Automatic control is turned off in Setup/);
    assert.match(appSource, /input\.disabled = ambientOverride\.active/);
    assert.match(styleSource, /\.helio-slider-row\.is-overridden input\[type="range"\] \{[\s\S]*opacity: 0\.38;[\s\S]*cursor: not-allowed;/);
    assert.match(styleSource, /\.helio-brightness-override \{[\s\S]*border-left: 2px solid var\(--helio-gold\);/);
});

test("HELIO commands form a vertical rail and view toggles overlay the main preview", async () => {
    const appSource = await readFile(new URL("../../src/ui/app.js", import.meta.url), "utf8");
    const styleSource = await readFile(new URL("../../src/ui/style.css", import.meta.url), "utf8");

    assert.match(appSource, /heroActions\.append\(power, openPreview, menu\);/);
    assert.match(appSource, /cameraModes\.append\(makeHelioTiltToggle\(\), makeHelioSwivelToggle\(\)\);[\s\S]*previewOverlays\.append\(makeHelioWallToggle\(\), cameraModes\);/);
    assert.match(appSource, /stage\.append\(info, canvasWrap\);/);
    assert.match(styleSource, /\.helio-hero-actions \{[\s\S]*flex-direction: column;[\s\S]*align-items: stretch;/);
    assert.match(styleSource, /\.helio-preview-overlays \{[\s\S]*position: absolute;[\s\S]*justify-content: space-between;/);
    assert.match(styleSource, /\.helio-preview-camera-modes \{[\s\S]*display: flex;/);
    assert.match(styleSource, /body\[data-brand="helio"\] \.helio-stage \{[\s\S]*grid-template-columns: minmax\(150px, 190px\) minmax\(520px, 1fr\);/);
    assert.match(styleSource, /@media \(max-width: 720px\) \{[\s\S]*body\[data-brand="helio"\] \.helio-stage \{[\s\S]*grid-template-columns: 1fr;/);
});

test("HELIO UI auto-configures audio scenes before starting them", async () => {
    const appSource = await readFile(new URL("../../src/ui/app.js", import.meta.url), "utf8");

    assert.match(appSource, /const needsAudio = helioSceneUsesAudio\(mode\) && !audioReady;/);
    assert.match(appSource, /const needsAudio = helioPlaylistUsesAudio\(playlist\) && !audioReady;/);
    assert.match(appSource, /const HELIO_DEFAULT_AUDIO = \{[\s\S]*sckPin: 40,[\s\S]*wsPin: 38,[\s\S]*sdPin: 39,[\s\S]*sampleRate: 2,[\s\S]*floor: 75,[\s\S]*gain: 1,[\s\S]*syncPort: 11988,[\s\S]*\};/);
    assert.match(appSource, /async function ensureHelioAudioReady\(status\)/);
    assert.match(appSource, /for \(const \[control, value\] of Object\.entries\(HELIO_DEFAULT_AUDIO\)\)/);
    assert.match(appSource, /if \(helioSceneUsesAudio\(mode\) && !\(await ensureHelioAudioReady\(status\)\)\)/);
    assert.match(appSource, /if \(helioPlaylistUsesAudio\(playlist\) && !\(await ensureHelioAudioReady\(status\)\)\)/);
    assert.doesNotMatch(appSource, /sendControl\(audio\.name,\s*"simulate"/);
    assert.doesNotMatch(appSource, /btn\.disabled = needsAudio;/);
    assert.doesNotMatch(appSource, /play\.disabled = needsAudio;/);
});

test("HELIO firmware keeps the true LED preview stream wired", async () => {
    const mainSource = await readFile(new URL("../../src/main.cpp", import.meta.url), "utf8");

    assert.match(mainSource, /auto\* preview = static_cast<mm::PreviewDriver\*>\(mm::ModuleFactory::create\("PreviewDriver"\)\);/);
    assert.match(mainSource, /drivers->addChild\(preview\);/);
    assert.match(mainSource, /preview->setBroadcaster\(httpServer\);/);
    assert.doesNotMatch(mainSource, /if \(!helioProduct\) \{[\s\S]*PreviewDriver/);
});

test("HELIO orchestration API uses HELIO names", async () => {
    const appSource = await readFile(new URL("../../src/ui/app.js", import.meta.url), "utf8");
    const catalogSource = await readFile(new URL("../../src/ui/helio-scenes.js", import.meta.url), "utf8");

    assert.doesNotMatch(`${appSource}\n${catalogSource}`, /\b(HELIO_WLED|helioWled)/);
});
