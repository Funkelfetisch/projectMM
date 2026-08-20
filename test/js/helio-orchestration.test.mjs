import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

test("firmware confirms OTA only after scheduler setup", () => {
    const source = readFileSync(new URL("../../src/main.cpp", import.meta.url), "utf8");
    const setup = source.indexOf("scheduler.setup();");
    const valid = source.indexOf("mm::platform::markOtaAppValid();");
    assert.ok(setup >= 0 && valid > setup);
});
import { helioAudioStartEnabled, planHelioEffectActivation } from "../../src/ui/helio-orchestration.js";

const controls = (...pairs) => pairs.map(([name, value, type = "uint8"]) => ({ name, value, type }));

test("HELIO activation keeps a repeated active scene on and disables other effects", () => {
    const effects = [
        { name: "HelioCandleMult", type: "HelioCandleMultiEffect", enabled: true, controls: controls(["speed", 53]) },
        { name: "PixelblazeCompa", type: "PixelblazeCompatEffect", enabled: true, controls: [] },
        { name: "Solid", type: "SolidEffect", enabled: false, controls: [] },
    ];

    assert.deepEqual(planHelioEffectActivation(effects, "HelioCandleMult", { speed: 53 }), [
        { module: "PixelblazeCompa", control: "enabled", value: false },
    ]);
});

test("HELIO activation disables other effects, applies controls, then enables the scene", () => {
    const effects = [
        { name: "HelioRipple", type: "RingsEffect", enabled: false, controls: controls(["speed", 42], ["palette", 0, "palette"]) },
        { name: "HelioLake", type: "HelioLakeEffect", enabled: true, controls: [] },
    ];

    const ops = planHelioEffectActivation(effects, "HelioRipple", { speed: 80, palette: 99 }, {
        controlEquals: (ctrl, next) => Number(ctrl.value) === Number(next),
        controlValueForSend: (ctrl, value) => ctrl.type === "palette" ? 3 : value,
    });

    assert.deepEqual(ops, [
        { module: "HelioLake", control: "enabled", value: false },
        { module: "HelioRipple", control: "speed", value: 80 },
        { module: "HelioRipple", control: "palette", value: 3 },
        { module: "HelioRipple", control: "enabled", value: true },
    ]);
});

test("HELIO activation ignores missing targets and missing controls", () => {
    const effects = [
        { name: "HelioRipple", type: "RingsEffect", enabled: true, controls: controls(["speed", 42]) },
    ];

    assert.deepEqual(planHelioEffectActivation(effects, "Missing", { speed: 80 }), []);
    assert.deepEqual(planHelioEffectActivation(effects, "HelioRipple", { unknown: 1 }), []);
});

test("HELIO audio start requires an enabled configured audio module", () => {
    const configured = {
        enabled: true,
        controls: controls(["sckPin", 40, "pin"], ["wsPin", 38, "pin"], ["sdPin", 39, "pin"]),
    };

    assert.equal(helioAudioStartEnabled(configured), true);
    assert.equal(helioAudioStartEnabled({ ...configured, enabled: false }), false);
    assert.equal(helioAudioStartEnabled({ enabled: true, controls: controls(["sckPin", -1, "pin"], ["wsPin", 38, "pin"], ["sdPin", 39, "pin"]) }), false);
    assert.equal(helioAudioStartEnabled(null), false);
});

test("HELIO playlists self-schedule only after each scene apply finishes", () => {
    const app = readFileSync(new URL("../../src/ui/app.js", import.meta.url), "utf8");
    const start = app.indexOf("async function applyHelioPlaylist");
    const end = app.indexOf("function updateHelioHome");
    const playlistBody = app.slice(start, end);

    assert.ok(start >= 0 && end > start);
    assert.doesNotMatch(playlistBody, /setInterval\(/);
    assert.match(playlistBody, /const durationMs = Math\.max\(3, Number\(playlist\.duration\) \|\| 10\) \* 1000;/);
    assert.match(playlistBody, /run\.timer = setTimeout\(step, durationMs\);/);
    assert.match(playlistBody, /if \(!applied\) \{[\s\S]*stopHelioPlaylist\(\);[\s\S]*return;[\s\S]*\}/);
    assert.match(playlistBody, /if \(helioPlaylistRun === run\) schedule\(\);/);
    assert.match(app, /section\.dataset\.helioPlaylist = playlist\.id;/);
    assert.match(app, /stop\.dataset\.helioPlaylistStop = playlist\.id;/);
    assert.match(app, /function syncHelioPlaylistControls\(\) \{[\s\S]*section\.classList\.toggle\("active", active\);[\s\S]*stop\.disabled = !active;/);
    assert.match(app, /function stopHelioPlaylist\(\) \{[\s\S]*clearTimeout\(helioPlaylistRun\.timer\);[\s\S]*syncHelioPlaylistControls\(\);/);
    assert.match(playlistBody, /helioPlaylistRun = run;\s*syncHelioPlaylistControls\(\);/);
});

test("HELIO home exposes the hidden project controls as Advanced", () => {
    const app = readFileSync(new URL("../../src/ui/app.js", import.meta.url), "utf8");

    assert.match(app, /menu\.textContent = "Advanced";/);
    assert.match(app, /menu\.setAttribute\("aria-label", "Open advanced controls"\);/);
    assert.match(app, /navToggle\.setAttribute\("aria-label", helio \? "Open advanced controls" : "Toggle navigation"\);/);
    assert.match(app, /rebootBtn\.hidden = helio \|\| chipCtrl\.value === "desktop";/);
    assert.match(app, /statsEl\.hidden = helio;/);
    assert.doesNotMatch(app, /menu\.textContent = "Menu";/);
});

test("HELIO home keeps only product actions above the preview", () => {
    const app = readFileSync(new URL("../../src/ui/app.js", import.meta.url), "utf8");

    assert.match(app, /info\.append\(eyebrow, heroActions\);/);
    assert.match(app, /openPreview\.textContent = "Preview";/);
    assert.match(app, /menu\.textContent = "Advanced";/);
    assert.doesNotMatch(app, /helioMetric\(/);
});

test("HELIO scene cards show apply failures instead of swallowing click errors", () => {
    const app = readFileSync(new URL("../../src/ui/app.js", import.meta.url), "utf8");
    const start = app.indexOf("function renderHelioSceneCard");
    const end = app.indexOf("function renderHelioControlPanel");
    const body = app.slice(start, end);

    assert.ok(start >= 0 && end > start);
    assert.match(body, /event\.preventDefault\(\);/);
    assert.match(body, /applyHelioMode\(mode, status\)\.catch\(err =>/);
    assert.match(body, /status\.textContent = "apply failed";/);
});

test("HELIO scene activation reuses one native scene slot", () => {
    const app = readFileSync(new URL("../../src/ui/app.js", import.meta.url), "utf8");
    const start = app.indexOf("async function applyHelioMode");
    const end = app.indexOf("async function applyHelioPlaylist");
    const body = app.slice(start, end);

    assert.ok(start >= 0 && end > start);
    assert.match(app, /function helioSceneReplacementSlot\(targetType = ""\) \{[\s\S]*const active = helioActiveEffect\(\);[\s\S]*active\.type !== targetType[\s\S]*return active;[\s\S]*PixelblazeCompatEffect[\s\S]*isHelioPortEffectType[\s\S]*isHelioFactoryScene\(effect\)/);
    assert.match(body, /let replacement = null;[\s\S]*let changed = false;/);
    assert.match(body, /if \(!\(state\?\.modules\?\.length\)\) \{[\s\S]*return false;[\s\S]*\}[\s\S]*ensureHelioAudioReady/);
    assert.match(body, /const mutationOpts = \{ refresh: false \};/);
    assert.match(body, /const slot = helioSceneReplacementSlot\(mode\.type\);/);
    assert.match(body, /if \(slot\) \{[\s\S]*replacement = await replaceModule\(slot\.name, mode\.type, mutationOpts\);[\s\S]*changed = !!replacement;[\s\S]*\}/);
    assert.match(body, /if \(!changed\) \{[\s\S]*changed = await addModule\(mode\.type, layer\.name, mutationOpts\);[\s\S]*\}/);
    assert.match(body, /if \(!changed\) \{[\s\S]*const activeSlot = helioActiveEffect\(\);[\s\S]*replacement = await replaceModule\(activeSlot\.name, mode\.type, mutationOpts\);[\s\S]*changed = !!replacement;[\s\S]*\}/);
    assert.match(body, /if \(replacement && replacement !== true\) target = replacement;[\s\S]*if \(!target\) \{[\s\S]*await refreshStateData\(0, API_FETCH_TIMEOUT_MS\);[\s\S]*target = helioEffects\(\)\.find\(e => e\.type === mode\.type\);/);
    assert.equal((body.match(/refreshStateData/g) || []).length, 1);
    assert.doesNotMatch(body, /pruneHelioSceneModules|applyHelioScenePalette\(mode, \{ force: true \}\)|await waitMs\(120\)/);
    assert.match(app, /if \(data\.resync === true\) \{[\s\S]*if \(helioSceneResyncExpected\) \{[\s\S]*helioSceneResyncExpected = false;[\s\S]*return;[\s\S]*\}[\s\S]*refetchState/);
});

test("HELIO scene activation preserves mounted previews while controls change", () => {
    const app = readFileSync(new URL("../../src/ui/app.js", import.meta.url), "utf8");
    const start = app.indexOf("async function applyHelioMode");
    const end = app.indexOf("async function applyHelioPlaylist");
    const body = app.slice(start, end);

    assert.match(app, /function refreshHelioControlPanel\(activeEffect = helioActiveEffect\(\)\) \{[\s\S]*current\.replaceWith\(renderHelioControlPanel\(activeEffect\)\);/);
    assert.match(app, /function syncHelioDashboardControls\(\) \{[\s\S]*refreshHelioControlPanel\(\);[\s\S]*updateHelioHome\(\);/);
    assert.match(body, /const pendingTarget = helioEffects\(\)\.find\(effect => effect\.type === mode\.type\);[\s\S]*refreshHelioControlPanel\(helioEffectWithControlValues\(pendingTarget, mode\.controls\)\);/);
    assert.match(body, /syncHelioDashboardControls\(\);/);
    assert.doesNotMatch(body, /refetchState\(|renderCards\(/);
});
