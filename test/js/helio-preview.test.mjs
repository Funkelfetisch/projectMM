import { test } from "node:test";
import assert from "node:assert/strict";

import {
    HELIO_FACTORY_SCENES,
    isNativeHelioSceneType,
} from "../../src/ui/helio-scenes.js";
import {
    HELIO_PREVIEW_AUDIO,
    helioPresetPreviewColor,
    helioScenePresetIndex,
    paletteAt,
    rgbToRgbw,
    rgbwToRgb,
} from "../../src/ui/helio-preview.js";

const TEST_PALETTE = [
    [0, 0, 0],
    [255, 124, 0],
    [255, 230, 170],
    [58, 150, 255],
    [111, 255, 0],
];

function brightness(c) {
    return Math.max(c[0] || 0, c[1] || 0, c[2] || 0);
}

function quantizedColor(c) {
    return c.map(v => Math.round((Number(v) || 0) / 12)).join(",");
}

test("HELIO preview sampler renders every factory preset visibly", () => {
    for (const scene of HELIO_FACTORY_SCENES) {
        const samples = [];
        for (const t of [0.2, 1.15, 2.7]) {
            for (let i = 0; i < 40; i++) {
                samples.push(helioPresetPreviewColor(
                    scene.presetIndex,
                    scene.controls,
                    TEST_PALETTE,
                    t,
                    i / 40,
                    (i % 9) / 8,
                    HELIO_PREVIEW_AUDIO,
                    { ledCount: 40, paletteByIndex: (_index, fallback) => fallback },
                ));
            }
        }

        const lit = samples.filter(c => brightness(c) > 24);
        const distinct = new Set(lit.map(quantizedColor));

        assert.ok(Math.max(...samples.map(brightness)) > 36, `${scene.id} preview is not black`);
        assert.ok(lit.length >= 8, `${scene.id} preview has enough lit samples`);
        assert.ok(distinct.size >= 3, `${scene.id} preview has visible variation`);
    }
});

test("HELIO preview sampler maps native scene cards back to their preset", () => {
    const nativeScenes = HELIO_FACTORY_SCENES.filter(scene => isNativeHelioSceneType(scene.type));

    assert.ok(nativeScenes.length > 0);
    for (const scene of nativeScenes) {
        assert.equal(helioScenePresetIndex({ type: scene.type, controls: [] }, 0), scene.presetIndex, scene.id);
    }
});

test("HELIO preview timing follows scene speed controls", () => {
    const sampleStrip = (preset, controls) => Array.from({ length: 24 }, (_value, i) =>
        helioPresetPreviewColor(
            preset,
            controls,
            TEST_PALETTE,
            1.25,
            i / 24,
            (i % 7) / 6,
            HELIO_PREVIEW_AUDIO,
            { ledCount: 24, paletteByIndex: (_index, fallback) => fallback },
        ));

    assert.notDeepEqual(sampleStrip(4, { speed: 8 }), sampleStrip(4, { speed: 224 }));
    assert.notDeepEqual(sampleStrip(5, { speed: 6 }), sampleStrip(5, { speed: 220 }));
});

test("HELIO palette sampling uses the firmware's cyclic 8-bit wheel", () => {
    const palette = Array.from({ length: 16 }, (_value, index) => [index * 16, index, 0]);

    assert.deepEqual(paletteAt(palette, 0), [0, 0, 0]);
    assert.deepEqual(paletteAt(palette, 8 / 255), [8, 0, 0]);
    assert.deepEqual(paletteAt(palette, 16 / 255), [16, 1, 0]);
    assert.deepEqual(paletteAt(palette, 255 / 255), [0, 0, 0]);
    assert.deepEqual(paletteAt(palette, 256 / 255), [0, 0, 0]);
});

test("HELIO scene simulation includes the physical dual warm-white output", () => {
    const palette = Array.from({ length: 16 }, () => [100, 60, 20]);
    const color = helioPresetPreviewColor(5, { speed: 0, output: 255 }, palette, 0, 0, 0.5);

    assert.deepEqual(color, [120, 74, 29]);
});

test("HELIO native sparse previews match their physical LED footprints", () => {
    const sampleNativeStrip = (sceneId, t) => {
        const scene = HELIO_FACTORY_SCENES.find(item => item.id === sceneId);
        return Array.from({ length: 311 }, (_value, i) => helioPresetPreviewColor(
            scene.presetIndex,
            scene.controls,
            TEST_PALETTE,
            t,
            i / 311,
            0.5,
            HELIO_PREVIEW_AUDIO,
            { ledCount: 311, paletteByIndex: (_index, fallback) => fallback },
        ));
    };

    const amberLit = sampleNativeStrip("freqmap-amber", 1.4).filter(c => brightness(c) > 0).length;
    const jugglesLit = sampleNativeStrip("juggles", 1.4).filter(c => brightness(c) > 0).length;

    assert.ok(amberLit >= 80 && amberLit <= 160, `Amber Bands keeps the saved slow frequency trail (${amberLit})`);
    assert.ok(jugglesLit >= 40 && jugglesLit <= 120, `Juggling Dots keeps eight moving dots and short trails (${jugglesLit})`);
});

test("HELIO color picker extracts the 3000K white channel", () => {
    const warm = [255, 214, 148];
    const channels = rgbToRgbw(warm);

    assert.deepEqual(channels, [0, 38, 38, 255]);
    assert.deepEqual(rgbwToRgb(channels), warm);
    assert.deepEqual(rgbToRgbw([255, 0, 0]), [255, 0, 0, 0]);
});
