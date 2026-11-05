import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const source = readFileSync(new URL('../../src/ui/helio-preview3d.js', import.meta.url), 'utf8');

function functionSource(name) {
    const start = source.indexOf(`function ${name}(`);
    assert.ok(start >= 0, name);
    const end = source.indexOf('\n}\n', start);
    return source.slice(start, end + 2);
}

function decoder() {
    const context = vm.createContext({ DataView, Uint8Array, Float32Array, Set, Math, console,
        performance: { now: () => 100 }, document: { getElementById: () => null } });
    vm.runInContext(`var gl = null, helioPreview_ = null, previewCoords_ = null, previewCoordCount_ = 0, previewMaxDim_ = 1,
        previewStride_ = 1, previewBox_ = null, previewEpoch_ = null, requestPreviewTable_ = null,
        frameStamps_ = [], effectiveFps_ = 0, liveFrameListeners_ = new Set();
        function initWebGL() {}
        function drawLights(rgb) { if (!gl) initWebGL(); if (!gl) return; }
        ${['renderPreviewBinary', 'parsePreviewCoords', 'renderPreviewFrame', 'notifyLiveFrame', 'measureFrameRate', 'updatePreviewStatus'].map(functionSource).join('\n')}`, context);
    return { context };
}

test('a browser without WebGL still hands each preview frame to the live listeners', () => {
    const { context } = decoder();
    const frames = [];
    context.liveFrameListeners_.add(frame => frames.push({ count: frame.count, rgb: [...frame.rgb], coords: frame.coords.length }));
    // Coordinate table: 2 lights in a 2 by 1 box, stride 1, epoch 4.
    context.renderPreviewBinary(new Uint8Array([0x03, 2, 0, 0, 0, 1, 0, 0, 1, 0, 4, 0, 0, 0, 1, 0, 0]).buffer);
    // Colour frame for the same light set and epoch.
    context.renderPreviewBinary(new Uint8Array([0x02, 2, 0, 0, 0, 1, 0, 4, 0, 10, 20, 30, 40, 50, 60]).buffer);
    assert.equal(context.gl, null);
    assert.deepEqual(frames, [{ count: 2, rgb: [10, 20, 30, 40, 50, 60], coords: 6 }]);
});
