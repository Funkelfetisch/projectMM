import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

test('preview drag ends safely on cancellation, capture loss and the owning pointer release', () => {
    const source = readFileSync(new URL('../../src/ui/helio-preview3d.js', import.meta.url), 'utf8');
    const start = source.indexOf('    let drag = null;');
    const end = source.indexOf('\n    applyMode();', start);
    assert.ok(start >= 0 && end > start);
    const listeners = {}, classes = new Set(), saved = [];
    let placements = 0;
    const bar = { addEventListener: (name, fn) => { listeners[name] = fn; }, setPointerCapture() {}, releasePointerCapture() {} };
    const pane = { classList: { add: x => classes.add(x), remove: x => classes.delete(x) }, style: {}, offsetWidth: 100, offsetHeight: 100,
        getBoundingClientRect: () => ({ left: 100, top: 100, width: 100, height: 100 }) };
    const context = vm.createContext({ bar, pane, ws: { classList: { contains: () => true } },
        window: { innerWidth: 800, innerHeight: 600 }, corner: 'br', dismissed: false, forcePip: false,
        savePrefs: value => saved.push(value.corner), placeCorner: () => placements++ });
    vm.runInContext(source.slice(start, end), context);
    const event = (pointerId, x = 120) => ({ pointerId, clientX: x, clientY: 120, target: { closest: () => false }, preventDefault() {} });
    listeners.pointerdown(event(1)); listeners.pointermove(event(2, 400));
    assert.equal(pane.style.left, undefined);
    listeners.pointermove(event(1, 400)); assert.equal(pane.style.left, '380px');
    listeners.pointercancel(event(1)); assert.equal(classes.has('dragging'), false);
    assert.equal(placements, 1); assert.deepEqual(saved, []);
    listeners.pointerdown(event(3)); listeners.lostpointercapture(event(3));
    assert.equal(classes.has('dragging'), false); assert.equal(placements, 2);
    listeners.pointerdown(event(4)); listeners.pointerup(event(5));
    assert.equal(classes.has('dragging'), true);
    listeners.pointerup(event(4)); assert.equal(classes.has('dragging'), false);
    assert.deepEqual(saved, ['tl']); assert.equal(placements, 3);
});
