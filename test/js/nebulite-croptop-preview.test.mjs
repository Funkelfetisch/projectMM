import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const source = readFileSync(new URL('../../src/ui/nebulite-croptop-preview.js', import.meta.url), 'utf8');
const { drawHistoricalCroptop } = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);

function fixture() {
    const calls = [], samples = [], attributes = {};
    const ctx = {
        fillRect() {}, save() {}, restore() {}, translate() {}, scale() {}, beginPath() {}, closePath() {},
        moveTo() {}, lineTo() {}, fill() { calls.push('polygon'); },
        createLinearGradient() { return { addColorStop(offset, color) { calls.push([offset, color]); } }; },
        drawImage(image) { calls.push(image.name); },
    };
    const canvas = { width: 384, height: 384, dataset: {}, getContext: () => ctx,
        setAttribute: (key, value) => { attributes[key] = value; } };
    const assets = { texture: { name: 'texture', complete: true, naturalWidth: 400 },
        garment: { name: 'garment', complete: true, naturalWidth: 400 } };
    return { calls, samples, attributes, canvas, assets,
        sample(x, y, t) { samples.push([x, y, t]); return [x * 255, 0, 0]; } };
}

test('historical crop-top renders eight RGB source positions and excludes the white output', () => {
    const f = fixture();
    assert.equal(drawHistoricalCroptop(f.canvas, 2, f.sample, 9, f.assets), true);
    assert.deepEqual(f.samples.map(([x]) => x), Array.from({ length: 8 }, (_, i) => (i + 0.5) / 9));
    assert.equal(f.calls.filter(x => x === 'polygon').length, 6);
    assert.deepEqual(f.calls.slice(-2), ['texture', 'garment']);
    assert.equal(f.canvas.dataset.previewRenderer, 'historical-croptop-simulation');
    assert.match(f.attributes['aria-label'], /historical.*white output not shown/);
});

test('unready artwork and incompatible logical lengths fall back without sampling', () => {
    const f = fixture();
    f.assets.garment.complete = false;
    assert.equal(drawHistoricalCroptop(f.canvas, 0, f.sample, 9, f.assets), false);
    f.assets.garment.complete = true;
    assert.equal(drawHistoricalCroptop(f.canvas, 0, f.sample, 91, f.assets), false);
    assert.deepEqual(f.samples, []);
});

test('classic crop-top samples its eight outputs without a fictitious white slot', () => {
    const f = fixture();
    assert.equal(drawHistoricalCroptop(f.canvas, 0, f.sample, 8, f.assets), true);
    assert.deepEqual(f.samples.map(([x]) => x), Array.from({length: 8}, (_, i) => (i + 0.5) / 8));
    assert.doesNotMatch(f.attributes['aria-label'], /white output/);
});
