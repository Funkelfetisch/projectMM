import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

globalThis.window = {
    matchMedia: () => ({ matches: false }),
};

const {
    HelioTrace,
    buildHelioReflectionColors,
    buildHelioWallColors,
    drawHelioTraceWebGLPreview,
    drawHelioTracePreview,
    helioTracePreviewSampleCount,
    helioTracePreviewTargetSize,
    helioSwivelPosition,
    helioTiltPitch,
} = await import("../../src/ui/heliotrace.js");

function fakeCanvas(width, height, opts = {}) {
    return {
        width: opts.backingWidth ?? 300,
        height: opts.backingHeight ?? 150,
        clientWidth: opts.clientWidth ?? 0,
        clientHeight: opts.clientHeight ?? 0,
        getBoundingClientRect: () => ({ width, height }),
    };
}

function fakeDrawableCanvas(width, height) {
    const calls = {
        arcs: [],
        ellipseCount: 0,
        fills: 0,
        strokes: 0,
    };
    const gradient = { addColorStop: () => {} };
    const ctx = {
        clearRect: () => {},
        createLinearGradient: () => gradient,
        createRadialGradient: () => gradient,
        fillRect: () => { calls.fills++; },
        save: () => {},
        restore: () => {},
        translate: () => {},
        rotate: () => {},
        beginPath: () => {},
        clip: () => {},
        arc: (x, y, r) => calls.arcs.push({ x, y, r }),
        stroke: () => { calls.strokes++; },
        fill: () => { calls.fills++; },
        ellipse: () => { calls.ellipseCount++; },
    };
    return {
        width,
        height,
        clientWidth: width,
        clientHeight: height,
        getBoundingClientRect: () => ({ width, height }),
        getContext: () => ctx,
        dataset: {},
        calls,
    };
}

test("HELIO card preview uses a high-resolution square backing store", () => {
    const canvas = fakeCanvas(320, 320);

    const target = helioTracePreviewTargetSize(canvas, 3.0);

    assert.deepEqual(target, { cssW: 320, cssH: 320, w: 960, h: 960 });
    assert.equal(canvas.width, 960);
    assert.equal(canvas.height, 960);
});

test("HELIO preview sizing falls back to canvas dimensions when layout is not ready", () => {
    const canvas = fakeCanvas(0, 0, { backingWidth: 384, backingHeight: 384 });

    const target = helioTracePreviewTargetSize(canvas, 2.5);

    assert.deepEqual(target, { cssW: 384, cssH: 384, w: 960, h: 960 });
    assert.equal(canvas.width, 960);
    assert.equal(canvas.height, 960);
});

test("HELIO preview sizing keeps zero-height card fallbacks square", () => {
    const canvas = fakeCanvas(0, 0, { backingWidth: 300, backingHeight: 150 });

    const target = helioTracePreviewTargetSize(canvas, 2.0);

    assert.deepEqual(target, { cssW: 300, cssH: 300, w: 600, h: 600 });
    assert.equal(canvas.width, 600);
    assert.equal(canvas.height, 600);
});

test("HELIO card preview caps very large backing stores", () => {
    const canvas = fakeCanvas(360, 360);

    const target = helioTracePreviewTargetSize(canvas, 10.0, 2560);

    assert.deepEqual(target, { cssW: 360, cssH: 360, w: 2560, h: 2560 });
    assert.equal(canvas.width, 2560);
    assert.equal(canvas.height, 2560);
});

test("HELIO scene cards render above the displayed card resolution", () => {
    const canvas = fakeCanvas(216, 216);

    const target = helioTracePreviewTargetSize(canvas, 12.0, 3072);

    assert.deepEqual(target, { cssW: 216, cssH: 216, w: 2592, h: 2592 });
    assert.equal(canvas.width, 2592);
    assert.equal(canvas.height, 2592);
});

test("HELIO card previews keep quality and stable motion while throttling page work", () => {
    const app = readFileSync(new URL("../../src/ui/app.js", import.meta.url), "utf8");

    assert.match(app, /const HELIO_PREVIEW_DEFAULTS = Object\.freeze\(\{[\s\S]*mainBounces: 6,[\s\S]*cardBounces: 5,[\s\S]*mainDpr: 1\.25,[\s\S]*cardDpr: 1\.8,[\s\S]*cardMaxBackingSize: 640,[\s\S]*mainFps: 24,[\s\S]*cardFps: 30,[\s\S]*cardBatch: 2,/);
    assert.match(app, /const HELIO_PREVIEW_SETTINGS_VERSION = 5;/);
    assert.match(app, /const previousDefaultCadence =[\s\S]*Number\(saved\.cardFps\) === 24[\s\S]*Number\(saved\.cardBatch\) === 4/);
    assert.match(app, /const previousDefaultBounces =[\s\S]*Number\(saved\.mainBounces\) === 22[\s\S]*Number\(saved\.cardBounces\) === 14/);
    assert.match(app, /const HELIO_PREVIEW_VIEWPORT_MARGIN_PX = 0;/);
    assert.doesNotMatch(app, /HELIO_ACTIVE_PREVIEW_INTERVAL_MS/);
    assert.doesNotMatch(app, /HELIO_PREVIEW_SEED_CARD_COUNT/);
    assert.match(app, /function helioPreviewDetail\(kind = "main"\) \{[\s\S]*const cameraMode = card \? "tilt" : helioPreviewCameraMode\(\);[\s\S]*bounces: card \? settings\.cardBounces : settings\.mainBounces,[\s\S]*maxDpr: card \? settings\.cardDpr : settings\.mainDpr,[\s\S]*reflectionSpread: settings\.reflectionSpread,[\s\S]*wallDistanceCm: settings\.wallDistanceCm,[\s\S]*autoSpin: false,[\s\S]*tiltAnimation: cameraMode === "tilt",[\s\S]*swivelAnimation: cameraMode === "swivel"/);
    assert.match(app, /function renderHelioPreviewSettings\(\) \{[\s\S]*HELIO_PREVIEW_SETTING_SPECS[\s\S]*dataset\.helioPreviewSetting[\s\S]*Reset preview/);
    assert.match(app, /window\.addEventListener\("scroll", requestHelioViewportPreviewRefresh, \{ passive: true \}\)/);
    assert.match(app, /queueHelioMissingVisiblePreviews\(now\)/);
    assert.match(app, /helioPreviewItems\(false, \{ missingOnly: true \}\)/);
    assert.match(app, /function queueHelioVisiblePreviews\(now, batchSize = helioPreviewSettings\(\)\.cardBatch\) \{[\s\S]*helioPreviewBatch = helioPreviewItems\(false\);[\s\S]*paintHelioPreviewBatch\(now, batchSize\);[\s\S]*\}/);
    assert.match(app, /function helioPreviewNearViewport\(canvas\) \{[\s\S]*rect\.bottom >= -HELIO_PREVIEW_VIEWPORT_MARGIN_PX[\s\S]*rect\.top <= window\.innerHeight \+ HELIO_PREVIEW_VIEWPORT_MARGIN_PX/);
    assert.match(app, /function clearHelioPreviewRenderState\(canvas\) \{[\s\S]*delete canvas\.dataset\.previewRenderer;[\s\S]*delete canvas\.dataset\.previewCamera;[\s\S]*delete canvas\.dataset\.previewMax;[\s\S]*delete canvas\.dataset\.previewLit;[\s\S]*\}/);
    assert.match(app, /function helioCardPreviewTime\(canvas, now\) \{[\s\S]*Math\.min\(elapsed, frameMs \* 1\.5\)[\s\S]*return clock\.sceneTime;/);
    assert.match(app, /function paintHelioPreviewItem\(item, now\) \{[\s\S]*const t = helioCardPreviewTime\(item\.canvas, now\);/);
    assert.match(app, /if \(!force && !helioPreviewNearViewport\(canvas\)\) \{[\s\S]*clearHelioPreviewRenderState\(canvas\);[\s\S]*return;[\s\S]*\}/);
    assert.match(app, /else if \(now - lastHelioPreviewPaint >= 1000 \/ helioPreviewSettings\(\)\.cardFps\)/);
    assert.match(app, /paintHelioPreviewBatch\(now\);/);
    assert.match(app, /helioCardPreviewsPaused\(now\)/);
    assert.match(app, /pauseHelioCardPreviews\(2400\)/);
    assert.doesNotMatch(app, /seededHelioPreviewItems/);
    assert.match(app, /function resumeHelioCardPreviews\(\) \{[\s\S]*queueHelioVisiblePreviews\(now\)[\s\S]*\}/);
    assert.match(app, /else resumeHelioCardPreviews\(\);/);
    assert.match(app, /function helioCardPreviewsPaused[\s\S]*helioFloatingPreviewEl && !helioFloatingPreviewEl\.hidden/);
    assert.match(app, /function drawHelioMainPreview[\s\S]*helioFloatingPreviewEl && !helioFloatingPreviewEl\.hidden[\s\S]*helioTrace\.stop\(\)/);
    assert.match(app, /function openHelioFloatingPreview[\s\S]*helioTrace\.stop\(\);[\s\S]*syncHelioFloatingPreview\(\);/);
    assert.match(app, /function closeHelioFloatingPreview[\s\S]*helioFloatingTrace\.stop\(\);[\s\S]*drawHelioMainPreview\(\);[\s\S]*resumeHelioCardPreviews\(\);/);
});

test("HELIO one-second state pulses avoid hidden controls and trace restarts", () => {
    const app = readFileSync(new URL("../../src/ui/app.js", import.meta.url), "utf8");
    const trace = readFileSync(new URL("../../src/ui/heliotrace.js", import.meta.url), "utf8");
    const updateStart = app.indexOf("function updateValues");
    const genericStart = app.indexOf("function updateGenericModuleValues", updateStart);
    const liveStart = app.indexOf("function updateHelioLivePreview");
    const refreshStart = app.indexOf("function refreshHelioTrace", liveStart);
    const homeStart = app.indexOf("function updateHelioHome");
    const previewsStart = app.indexOf("function startHelioModePreviews", homeStart);
    const updateBody = app.slice(updateStart, genericStart);
    const liveBody = app.slice(liveStart, refreshStart);
    const homeBody = app.slice(homeStart, previewsStart);

    assert.match(updateBody, /helioDashboardActive[\s\S]*if \(!helioDashboardActive\) updateGenericModuleValues\(\);/);
    assert.doesNotMatch(updateBody, /allModules\(\)/);
    assert.doesNotMatch(liveBody, /drawHelioMainPreview|syncHelioFloatingPreview/);
    assert.match(liveBody, /drawHelioLivePreviewFrame\(helioLivePreviewSeenAt\);/);
    assert.match(liveBody, /function drawHelioLivePreviewFrame\(now, force = false\)[\s\S]*mainFps[\s\S]*helioFloatingTrace\.draw\(now\)[\s\S]*helioTrace\.draw\(now\)/);
    assert.doesNotMatch(homeBody, /refreshHelioTrace\(\)/);
    assert.match(trace, /const DEFAULT_TRACE_FPS = 30;/);
    assert.match(trace, /const frameInterval = 1000 \/ \(this\.cameraAnimationActive\(\) \? 60 : this\.targetFps\);[\s\S]*now - this\.lastFrameAt >= frameInterval - 0\.5/);
});

test("HELIO scene buttons expose stable mode attributes and immediate active state", () => {
    const app = readFileSync(new URL("../../src/ui/app.js", import.meta.url), "utf8");

    assert.match(app, /btn\.dataset\.helioMode = mode\.id;/);
    assert.match(app, /btn\.dataset\.scene = mode\.id;/);
    assert.match(app, /btn\.setAttribute\("aria-pressed", active\?\.type === mode\.type \? "true" : "false"\);/);
    assert.match(app, /function setHelioModeCardActive\(modeId\) \{[\s\S]*card\.classList\.toggle\("active", active\);[\s\S]*aria-pressed/);
    assert.match(app, /if \(!opts\.fromPlaylist\) setHelioModeCardActive\(mode\.id\);/);
});

test("HELIO playlist scene cards and presets share the high-quality trace path", () => {
    const app = readFileSync(new URL("../../src/ui/app.js", import.meta.url), "utf8");

    assert.match(app, /function drawHelioCardTracePreview\(canvas, t, sampler, ledCount = helioCurrentCount\(\), opts = \{\}\) \{[\s\S]*helioPreviewDetail\("card"\)[\s\S]*\}/);
    assert.doesNotMatch(app, /function drawHelioPlaylistCanvas/);
    assert.doesNotMatch(app, /\.helio-playlist-preview/);
    assert.match(app, /function paintHelioScenePreview\(canvas, t\) \{[\s\S]*const sampler =[\s\S]*makeHelioSampler[\s\S]*drawHelioCardTracePreview\(/);
    assert.match(app, /function drawPresetCanvas\(canvas, t\) \{[\s\S]*drawHelioCardTracePreview\(canvas, t, \(x, y, tt\) => effectColor/);
    assert.equal([...app.matchAll(/drawHelioTraceWebGLPreview\(/g)].length, 1);
});

test("HELIO wall and camera controls persist and reach the intended preview renderers", () => {
    const app = readFileSync(new URL("../../src/ui/app.js", import.meta.url), "utf8");

    assert.match(app, /const LS_HELIO_PREVIEW_WALL = "mm_helio_preview_wall";/);
    assert.match(app, /const LS_HELIO_PREVIEW_TILT = "mm_helio_preview_tilt";/);
    assert.match(app, /const LS_HELIO_PREVIEW_SWIVEL = "mm_helio_preview_swivel";/);
    assert.match(app, /function helioPreviewWallEnabled\(\) \{[\s\S]*LS_HELIO_PREVIEW_WALL[\s\S]*!== "0";/);
    assert.match(app, /function helioPreviewCameraMode\(\) \{[\s\S]*LS_HELIO_PREVIEW_SWIVEL[\s\S]*return "swivel";[\s\S]*LS_HELIO_PREVIEW_TILT[\s\S]*return "tilt";/);
    assert.match(app, /function setHelioPreviewCameraMode\(mode\) \{[\s\S]*LS_HELIO_PREVIEW_TILT[\s\S]*LS_HELIO_PREVIEW_SWIVEL[\s\S]*data-helio-tilt-toggle[\s\S]*data-helio-swivel-toggle/);
    assert.match(app, /function makeHelioWallToggle\(\) \{[\s\S]*"helioWallToggle"[\s\S]*setHelioPreviewWallEnabled/);
    assert.match(app, /function makeHelioTiltToggle\(\) \{[\s\S]*"helioTiltToggle"[\s\S]*setHelioPreviewTiltEnabled/);
    assert.match(app, /function makeHelioSwivelToggle\(\) \{[\s\S]*"helioSwivelToggle"[\s\S]*setHelioPreviewSwivelEnabled/);
    assert.match(app, /function configureHelioTraceRenderer[\s\S]*reflectionSpread: opts\.reflectionSpread,[\s\S]*wallDistanceCm: opts\.wallDistanceCm,[\s\S]*wall: helioPreviewWallEnabled\(\)[\s\S]*tiltAnimation: opts\.tiltAnimation === true,[\s\S]*swivelAnimation: opts\.swivelAnimation === true/);
    assert.match(app, /function drawHelioCardTracePreview[\s\S]*wall: helioPreviewWallEnabled\(\)/);
    assert.match(app, /cameraModes\.append\(makeHelioTiltToggle\(\), makeHelioSwivelToggle\(\)\);[\s\S]*previewOverlays\.append\(makeHelioWallToggle\(\), cameraModes\);/);
    assert.match(app, /heroActions\.append\(power, openPreview, menu\);/);
    assert.match(app, /bar\.append\(title, makeHelioWallToggle\(\), makeHelioTiltToggle\(\), makeHelioSwivelToggle\(\), close\);/);
});

test("HELIO active scene card simulates with current module controls", () => {
    const app = readFileSync(new URL("../../src/ui/app.js", import.meta.url), "utf8");
    const start = app.indexOf("function paintHelioScenePreview");
    const end = app.indexOf("function helioPreviewItems", start);
    const body = app.slice(start, end);

    assert.ok(start >= 0 && end > start);
    assert.match(body, /const active = helioActiveEffect\(\);/);
    assert.match(body, /active\?\.enabled !== false && active\?\.type === mode\.type/);
    assert.match(body, /const effect = usesActiveControls[\s\S]*\? active[\s\S]*Object\.entries\(mode\.controls/);
    assert.match(body, /const pal = usesActiveControls \? paletteFromState\(\) : helioPreviewPaletteForScene\(mode\);/);
    assert.match(body, /const sampler = makeHelioSampler\(effect, pal, \{ previewAudio: true \}\);/);
    assert.match(body, /previewSource = usesActiveControls \? "simulation-controls" : "simulation"/);
    assert.doesNotMatch(body, /sampleHelioLivePreview/);
});

test("HELIO live preview is strictly device-fed and treats only exact zero as off", () => {
    const app = readFileSync(new URL("../../src/ui/app.js", import.meta.url), "utf8");
    const start = app.indexOf("function sampleHelioLivePreview");
    const end = app.indexOf("function normalizeHelioPreviewSetting", start);
    const body = app.slice(start, end);

    assert.ok(start >= 0 && end > start);
    assert.doesNotMatch(app, /helioLivePreviewLevel > 2/);
    assert.doesNotMatch(app, /HELIO_UNLIT_PREVIEW_RGB/);
    assert.doesNotMatch(app, /helioTraceFallbackSampler/);
    assert.match(body, /if \(!helioLivePreviewFresh\(\)\) return \[0, 0, 0\];/);
    assert.doesNotMatch(body, /makeHelioSampler/);
    assert.match(app, /function configureHelioTraceRenderer[\s\S]*offThreshold: 0,/);
    assert.match(app, /function drawHelioCardTracePreview[\s\S]*offThreshold: opts\.live \? 0 : detail\.offThreshold/);
    assert.match(app, /canvas\.dataset\.livePreviewMax = String\(helioLivePreviewLevel\);/);
    assert.match(app, /ws\.onclose = \(\) => \{[\s\S]*clearHelioLivePreview\(\);/);
    assert.match(app, /const activated = await activateHelioEffect[\s\S]*clearHelioLivePreview\(\);/);
});

test("HELIO inactive LED packages are dim direct-only geometry", () => {
    const source = readFileSync(new URL("../../src/ui/heliotrace.js", import.meta.url), "utf8");

    assert.match(source, /const UNLIT_LED_RGB = \[3, 3, 3\];/);
    assert.match(source, /const DEFAULT_OFF_BRIGHTNESS = 0\.30;/);
    assert.match(source, /const DEFAULT_OFF_THRESHOLD = 12;/);
    assert.match(source, /const LED_TEXTURE_SUBPIXELS = 24;/);
    assert.match(source, /uniform sampler2D uLeds, uReflections, uWallLeds;/);
    assert.match(source, /vec3 reflectedLedTex[\s\S]*uLedUvPitch \* 0\.08[\s\S]*texture2D\(uReflections/);
    assert.match(source, /bandContribution\(P, dir, n0, exitPhase, dStr, uLedBlur \* 0\.18, tp, 1\.0\)/);
    assert.match(source, /bandContribution\(Pn, dir, nrm, phase, dStr, reflectionBlur, tp, 0\.0\)/);
    assert.match(source, /this\.reflectionData\[o\] = lit \? this\.data\[o\] : 0;/);
    assert.match(source, /gl\.texImage2D\(gl\.TEXTURE_2D, 0, gl\.RGBA, this\.ledCount \* sub, 1, 0,[\s\S]*gl\.RGBA, gl\.UNSIGNED_BYTE, this\.reflectionData\)/);
    assert.match(source, /if \(\(brightness\[i\] \|\| 0\) <= floor\) continue;/);
});

test("HELIO reflections preserve each lit source and exclude dark package colors", () => {
    const colors = Uint8Array.from([
        240, 0, 0,
        0, 180, 0,
        0, 180, 0,
        0, 180, 0,
        0, 180, 0,
    ]);
    const brightness = Uint8Array.from([240, 0, 0, 0, 0]);
    const reflected = buildHelioReflectionColors(colors, brightness, 12);

    assert.equal(reflected[0], 240);
    assert.equal(reflected[3], 0, "a lit source does not bleed into its neighbor");
    assert.equal(reflected[12], 0, "the strip boundary does not introduce a blurred copy");
    assert.equal(Array.from(reflected).filter((_value, index) => index % 3 === 1).reduce((sum, value) => sum + value, 0), 0);
    assert.equal(Array.from(reflected).filter((_value, index) => index % 3 === 2).reduce((sum, value) => sum + value, 0), 0);
});

test("HELIO wall projection diffuses lit sources circularly and excludes inactive LEDs", () => {
    const count = 72;
    const colors = new Uint8Array(count * 3);
    const brightness = new Uint8Array(count);
    colors[0] = 255;
    brightness[0] = 255;
    colors[3 + 1] = 255;
    const wall = buildHelioWallColors(colors, brightness, 12);

    assert.ok(wall[0] > wall[3], "the projected spot peaks at its emitting LED");
    assert.ok(wall[3] > 0, "the 110-degree cone diffuses into neighboring azimuths");
    assert.ok(wall[(count - 1) * 3] > 0, "the wall convolution wraps around the strip seam");
    assert.equal(Array.from(wall).filter((_value, index) => index % 3 === 1)
        .reduce((sum, value) => sum + value, 0), 0, "inactive package colors never reach the wall");
});

test("HELIO tilt traverses both poles and eases at the equator", () => {
    const epsilon = 0.001;

    assert.equal(helioTiltPitch(0), 0);
    assert.ok(Math.abs(helioTiltPitch(Math.PI / 2) - Math.PI / 2) < 1e-12);
    assert.ok(Math.abs(helioTiltPitch(Math.PI)) < 1e-12);
    assert.ok(Math.abs(helioTiltPitch(Math.PI * 1.5) + Math.PI / 2) < 1e-12);
    assert.ok(Math.abs(helioTiltPitch(epsilon) - helioTiltPitch(-epsilon)) < 1e-8,
        "equator velocity eases to zero");
});

test("HELIO swivel follows a 45-degree circle in front of the wall", () => {
    const start = helioSwivelPosition(0, 10);
    const quarter = helioSwivelPosition(Math.PI / 2, 10);

    assert.ok(Math.abs(Math.hypot(...start) - 10) < 1e-12);
    assert.ok(Math.abs(start[0] - start[2]) < 1e-12, "the orbit starts 45 degrees off the wall normal");
    assert.ok(Math.abs(quarter[0]) < 1e-12);
    assert.ok(Math.abs(quarter[1] - quarter[2]) < 1e-12);
    assert.ok(start[2] > 0 && quarter[2] > 0, "the camera remains in front of the rear wall");
});

test("HELIO canvas fallback samples near the rendered LED count", () => {
    assert.equal(helioTracePreviewSampleCount(311), 311);
    assert.equal(helioTracePreviewSampleCount(131), 256);
    assert.equal(helioTracePreviewSampleCount(1200), 1200);
    assert.equal(helioTracePreviewSampleCount(2400), 1536);
});

test("HELIO canvas fallback draws inactive packages once and never as reflections", () => {
    const canvas = fakeDrawableCanvas(220, 220);

    drawHelioTracePreview(canvas, 0, () => [0, 0, 0], {
        background: "transparent",
        glass: 0,
        showRim: false,
        ledCount: 131,
        maxDpr: 1,
    });

    assert.equal(canvas.calls.arcs.length, helioTracePreviewSampleCount(131));
    assert.equal(canvas.dataset.previewMax, "0");
    assert.equal(canvas.dataset.previewLit, "0");
});

test("HELIO canvas fallback keeps the glass rim fully off when requested", () => {
    const canvas = fakeDrawableCanvas(220, 220);

    drawHelioTracePreview(canvas, 0, () => [64, 128, 255], {
        background: "transparent",
        glass: 0,
        showRim: false,
        ledCount: 131,
        maxDpr: 1,
    });

    assert.equal(canvas.calls.ellipseCount, 0);
    assert.equal(canvas.dataset.previewMax, "255");
});

test("HELIO canvas fallback samples the LED strip instead of painting a shell", () => {
    const canvas = fakeDrawableCanvas(220, 220);
    const samples = [];

    drawHelioTracePreview(canvas, 1.25, (x, y, t) => {
        samples.push({ x, y, t });
        return [180, 90, 24];
    }, {
        background: "transparent",
        glass: 0,
        showRim: false,
        ledCount: 311,
        maxDpr: 1,
    });

    assert.equal(samples.length, 311);
    assert.equal(samples[0].x, 0);
    assert.ok(samples[samples.length - 1].x > 0.99);
    assert.ok(samples.every(sample => sample.y === 0.5 && sample.t === 1.25));
    assert.equal(canvas.dataset.previewMax, "180");
    assert.equal(canvas.dataset.previewLit, "311");
});

test("HELIO card preview fallback reports visible LED data when WebGL is unavailable", () => {
    const canvas = fakeDrawableCanvas(220, 220);

    const usedWebGl = drawHelioTraceWebGLPreview(canvas, 0.5, () => [32, 96, 220], {
        background: "transparent",
        glass: 0,
        showRim: false,
        ledCount: 131,
        maxDpr: 1,
    });

    assert.equal(usedWebGl, false);
    assert.equal(canvas.dataset.previewRenderer, "canvas");
    assert.equal(canvas.dataset.previewMax, "220");
    assert.equal(canvas.dataset.previewLit, "256");
});

test("HELIO trace defaults to visible inner-surface LEDs", () => {
    const renderer = new HelioTrace();

    assert.equal(renderer.ledFill, 1.0);
    assert.equal(renderer.ledGain, 8.0);
    assert.equal(renderer.reflectionSpread, 0.0);
    assert.equal(renderer.glow, 1.65);
    assert.equal(renderer.wallGlow, 1.25);
    assert.equal(renderer.wallDistanceCm, 25);
    assert.equal(renderer.offThreshold, 12);
    assert.equal(renderer.offBrightness, 0.30);
    assert.equal(renderer.wall, true);
    assert.equal(renderer.dichroic, 0.55);
    assert.equal(renderer.glass, 0.10);
    assert.equal(renderer.showRim, true);
    assert.equal(renderer.targetFps, 30);
    assert.equal(renderer.tiltAnimation, false);
    assert.equal(renderer.swivelAnimation, false);
});

test("HELIO trace preview detail accepts direct inner-surface bounces", () => {
    const renderer = new HelioTrace();

    renderer.setDetail({ bounces: 1, ledGain: 6.2, reflectionSpread: 2.4, offThreshold: 18, offBrightness: 0.12, attenuation: 0.72, cutoff: 0.08, glow: 2.1, wallGlow: 0.8, wallDistanceCm: 48, fps: 27, wall: false, tiltAnimation: true });

    assert.equal(renderer.bounces, 1);
    assert.equal(renderer.ledGain, 6.2);
    assert.equal(renderer.reflectionSpread, 2.4);
    assert.equal(renderer.offThreshold, 18);
    assert.equal(renderer.offBrightness, 0.12);
    assert.equal(renderer.attenuation, 0.72);
    assert.equal(renderer.cutoff, 0.08);
    assert.equal(renderer.targetFps, 27);
    assert.equal(renderer.glow, 2.1);
    assert.equal(renderer.wallGlow, 0.8);
    assert.equal(renderer.wallDistanceCm, 48);
    assert.equal(renderer.wall, false);
    assert.equal(renderer.tiltAnimation, true);
    assert.equal(renderer.swivelAnimation, false);

    renderer.tiltEpoch = 0;
    const poleCamera = renderer.camera(9000, renderer.geometry());
    assert.ok([...poleCamera.pos, ...poleCamera.r, ...poleCamera.u, ...poleCamera.f]
        .every(Number.isFinite), "the camera basis stays finite at an exact pole");
    assert.ok(Math.hypot(...poleCamera.r) > 0.99);

    renderer.setDetail({ swivelAnimation: true, tiltAnimation: false });
    assert.equal(renderer.tiltAnimation, false);
    assert.equal(renderer.swivelAnimation, true);
    renderer.swivelEpoch = 0;
    const swivelCamera = renderer.camera(0, renderer.geometry());
    assert.ok([...swivelCamera.pos, ...swivelCamera.r, ...swivelCamera.u, ...swivelCamera.f]
        .every(Number.isFinite));
    assert.ok(Math.abs(swivelCamera.pos[0] - swivelCamera.pos[2]) < 1e-12);
});

test("HELIO trace renderer preserves corrected emission RGB and discrete reflected LEDs", () => {
    const source = readFileSync(new URL("../../src/ui/heliotrace.js", import.meta.url), "utf8");
    const preview = readFileSync(new URL("../../src/ui/helio-preview.js", import.meta.url), "utf8");

    assert.doesNotMatch(source, /function warmWhiteRgb/);
    assert.match(source, /const LED_TEXTURE_SUBPIXELS = 24;/);
    assert.match(source, /uniform float [^;]*uLedGain, uLedBlur, uLedUvPitch, uLedPitch, uPixelWorld;/);
    assert.match(source, /uniform float uGlow, uWallGlow, uHasWall, uWallPlaneZ, uWallDistance, uRoomHalf;/);
    assert.match(source, /vec3 environmentColor\(vec3 ro, vec3 rd\)/);
    assert.match(source, /vec3 wallLedTex\(float u\)[\s\S]*texture2D\(uWallLeds/);
    assert.match(source, /vec3 bandContribution\(/);
    assert.match(source, /if\(direct < 0\.5\)[\s\S]*ledEmit\(reflectedLedTex\(u\), phase, dStr\)/);
    assert.doesNotMatch(source, /OES_standard_derivatives|fwidth\(/);
    assert.match(source, /float reflectedHalf = max\(uBandHalf \* haloSigma, uPixelWorld \* 0\.72\);/);
    assert.match(source, /return throughput \* sqrt\(face\) \* emitted \* \(core \+ halo\);/);
    assert.match(source, /gl\.disable\(gl\.DITHER\);/);
    assert.doesNotMatch(source, /uAmbientColor|previewAmbient|ambientDivisor/);
    assert.match(source, /bandContribution\(P, dir, n0, exitPhase, dStr, uLedBlur \* 0\.18, tp, 1\.0\)/);
    assert.match(source, /float reflectionBlur = uLedBlur \* \(0\.95 \+ float\(k\) \* 0\.16\);/);
    assert.match(preview, /clampByte\(g \+ w \* 0\.69\)/);
    assert.match(preview, /clampByte\(b \+ w \* 0\.43\)/);
});

test("HELIO wall light originates at the sphere center and is shadowed by the rim", () => {
    const source = readFileSync(new URL("../../src/ui/heliotrace.js", import.meta.url), "utf8");

    assert.match(source, /const ROOM_SIZE_MM = 3000;/);
    assert.match(source, /const LED_EMISSION_DEGREES = 110;/);
    assert.match(source, /bool roomIntersection\(vec3 ro, vec3 rd, out vec3 point, out float surface\)/);
    assert.match(source, /float frontZ = uWallPlaneZ \+ uRoomHalf \* 2\.0;/);
    assert.match(source, /vec3 centerEmitterColor\(float projectionU\)/);
    assert.match(source, /vec3 centerEmitterRadiance\(vec3 ro, vec3 rd\)/);
    assert.match(source, /vec3 sourceRay = roomPoint;/);
    assert.match(source, /float sourceDistance = length\(sourceRay\);/);
    assert.match(source, /float axialDistance = max\(-sourceRay\.z, 1e-5\);/);
    assert.match(source, /float emissionAzimuth = atan\(-sourceRay\.z, -sourceRay\.x\);/);
    assert.match(source, /float wallU = fract\(emissionAzimuth \/ \(2\.0 \* PI\) \+ 1\.0\);/);
    assert.match(source, /float elevationAngle = asin\(clamp\(sourceRay\.y \/ max\(sourceDistance, 1e-5\), -1\.0, 1\.0\)\);/);
    assert.match(source, /float emissionSigma = 0\.8154;/);
    assert.match(source, /float wallIncidence = axialDistance \/ max\(sourceDistance, 1e-5\);/);
    assert.match(source, /float centerToWall = max\(-uWallPlaneZ, 1e-5\);/);
    assert.match(source, /float distanceFalloff = centerToWall \* centerToWall \/[\s\S]*sourceDistance \* sourceDistance/);
    assert.match(source, /vec3 projected = centerEmitterColor\(wallU\) \* cone \* wallIncidence \* distanceFalloff;/);
    assert.match(source, /uFlangeHalf \* centerToWall \/ max\(uInnerR, 1e-5\)/);
    assert.match(source, /float rimShadow = mix\(0\.18, 1\.0,[\s\S]*projected \*= rimShadow;/);
    assert.match(source, /if\(hitOuter\) outCol \+= centerEmitterRadiance\(ro, rd\);/);
    assert.match(source, /this\.uniform1\("uWallPlaneZ", -\(geom\.R \+ geom\.wallDistance\)\);/);
    assert.doesNotMatch(source, /uInnerR \* 2\.0 \+ uWallDistance/);
    assert.doesNotMatch(source, /atan\(sourceRay\.y, sourceRay\.x\)/);
    assert.doesNotMatch(source, /cameraAngle/);
    assert.doesNotMatch(source, /sumR2|sumG2|sumB2|warmSpill \* \(broad/);
});

test("HELIO sphere stays clear instead of adding interior haze", () => {
    const source = readFileSync(new URL("../../src/ui/heliotrace.js", import.meta.url), "utf8");

    assert.match(source, /vec3 glass = vec3\(0\.025, 0\.07, 0\.055\) \* fres \* uGlass;/);
    assert.match(source, /float transmission = 1\.0 - fres \* 0\.025;/);
    assert.doesNotMatch(source, /volumeColor|glassFill|wallBounce/);
    assert.doesNotMatch(source, /const shell = ctx\.createRadialGradient/);
    assert.doesNotMatch(source, /const volume = ctx\.createRadialGradient/);
});

test("HELIO LED glow distributes energy radially around each emitter", () => {
    const source = readFileSync(new URL("../../src/ui/heliotrace.js", import.meta.url), "utf8");

    assert.match(source, /float radialPointWeight\([\s\S]*length\(vec2\(tangentDistance, verticalDistance\)\)/);
    assert.match(source, /centerWeight = radialPointWeight\(tangentDistance, P\.y, haloSigma\)/);
    assert.match(source, /previousWeight = radialPointWeight\(tangentDistance \+ uLedPitch, P\.y, haloSigma\)/);
    assert.match(source, /nextWeight = radialPointWeight\(tangentDistance - uLedPitch, P\.y, haloSigma\)/);
    assert.match(source, /this\.uniform1\("uLedUvPitch", 1 \/ Math\.max\(1, this\.ledCount\)\);/);
    assert.match(source, /ctx\.arc\(radius, 0, haloRadius, 0, TAU\);/);
    assert.doesNotMatch(source, /float bandDistance = abs\(P\.y\)/);
    assert.doesNotMatch(source, /exp\(-bandDistance/);
});
