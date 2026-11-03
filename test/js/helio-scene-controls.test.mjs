import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const url = source => `data:text/javascript;base64,${Buffer.from(source).toString('base64')}`;
const data = readFileSync(new URL('../../src/ui/helio-scene-data.js', import.meta.url), 'utf8');
const source = readFileSync(new URL('../../src/ui/helio-scene-controls.js', import.meta.url), 'utf8')
    .replace('"./helio-scene-data.js"', JSON.stringify(url(data)));
const { helioSceneControlMetadata, helioSceneControlLabel, helioSceneControlDescription, helioSceneControlVisible } = await import(url(source));
const scenesSource = readFileSync(new URL('../../src/ui/helio-scenes.js', import.meta.url), 'utf8')
    .replaceAll('"./helio-scene-data.js"', JSON.stringify(url(data)));
const { isHelioPortEffectType, helioFactorySceneForType, helioSceneForPresetIndex } = await import(url(scenesSource));
const app = readFileSync(new URL('../../src/ui/helio-app.js', import.meta.url), 'utf8');

function productFunction(name) {
    const start = app.indexOf(`\nfunction ${name}(`) + 1;
    assert.ok(start > 0, `${name} exists in the product dashboard`);
    return app.slice(start, app.indexOf('\n}', start) + 2);
}

test('HELIO sparkles show the WLED fade and spawn speed labels', () => {
    assert.equal(helioSceneControlLabel('colortwinkles', 'speed'), 'Fade speed');
    assert.equal(helioSceneControlLabel('HelioColorTwinklesEffect', 'effect intensity'), 'Spawn speed');
    assert.match(helioSceneControlDescription('colortwinkles', 'effect intensity'), /new sparkles/);
});

test('HELIO hides unused fine tuning and the unused lake intensity', () => {
    for (const name of ['custom 1', 'custom 2', 'custom 3', 'option 1', 'option 2', 'option 3']) {
        assert.equal(helioSceneControlVisible('colortwinkles', name), false);
    }
    assert.equal(helioSceneControlVisible('lake', 'effect intensity'), false);
    assert.equal(helioSceneControlVisible('pink-palette', 'effect intensity'), false);
});

test('HELIO equalizer exposes only its implemented band and smoothing controls', () => {
    assert.equal(helioSceneControlLabel('geq', 'custom 1'), '# of bands');
    assert.equal(helioSceneControlVisible('geq', 'custom 1'), true);
    assert.equal(helioSceneControlVisible('geq', 'custom 2'), false);
    assert.equal(helioSceneControlVisible('geq', 'option 1'), true);
    assert.equal(helioSceneControlVisible('geq', 'option 2'), true);
    assert.equal(helioSceneControlVisible('geq', 'option 3'), false);
});

test('HELIO frequency controls explain the implemented units and logarithmic mapping', () => {
    assert.equal(helioSceneControlLabel('freqwave', 'custom 1'), 'Low bin');
    assert.match(helioSceneControlDescription('freqwave', 'custom 1'), /Hz/);
    assert.match(helioSceneControlDescription('freqwave', 'option 1'), /logarithmically/);
    assert.match(helioSceneControlDescription('freqwave', 'custom 3'), /1 to 31/);
    assert.equal(helioSceneControlVisible('freqmap-amber', 'option 1'), false);
    assert.equal(helioSceneControlVisible('freqmap-wide', 'option 1'), true);
});

test('HELIO native rings use their own controls instead of WLED ripple controls', () => {
    assert.equal(helioSceneControlLabel('ripple', 'count'), 'Ring count');
    assert.equal(helioSceneControlVisible('ripple', 'speed'), true);
    assert.equal(helioSceneControlVisible('ripple', 'effect intensity'), false);
    assert.equal(helioSceneControlVisible('starburst', 'option 2'), false);
});

test('HELIO preserves normal labels for controls without effect metadata', () => {
    assert.equal(helioSceneControlLabel(null, 'fadeRate', 'Trail fade'), 'Trail fade');
    assert.deepEqual(helioSceneControlMetadata('unknown', 'speed'), {label:null, description:'', visible:true});
    assert.equal(helioSceneControlLabel({controlMetadata:{speed:{label:'Rate', description:'Movement rate.', visible:true}}}, 'speed'), 'Rate');
});

test('Phased, Lake and GEQ keep their port controls while Rings and Plasma use native controls', () => {
    for (const type of ['HelioPhasedEffect', 'HelioLakeEffect', 'HelioGEQEffect', 'HelioWledPresetsEffect']) {
        assert.equal(isHelioPortEffectType(type), true, `${type} renders through effect-specific port metadata`);
    }
    for (const type of ['RingsEffect', 'PlasmaEffect', 'UnknownEffect', 'HelioUnknownEffect']) {
        assert.equal(isHelioPortEffectType(type), false, `${type} uses its native controls`);
    }
});

test('the real port slider renderer omits unused metadata rows and displays the implemented Phased control', () => {
    const element = tagName => ({ tagName, dataset: {}, children: [],
        append(...children) { this.children.push(...children); },
        appendChild(child) { this.children.push(child); }, addEventListener() {} });
    let allocated = 0;
    const context = vm.createContext({
        document: { createElement: tagName => { allocated++; return element(tagName); } },
        helioControl: (module, name) => module.controls.find(control => control.name === name),
        helioFactorySceneForType, helioSceneForPresetIndex, helioSceneControlMetadata,
        helioPrettyLabel: name => name, helioFieldName: name => name,
        clampByte: value => Math.min(255, Math.max(0, value)),
    });
    vm.runInContext(['helioControlSceneForEffect', 'appendHelioControlHelp', 'helioSliderRow']
        .map(productFunction).join('\n'), context);
    const module = type => ({ type, name: `@preset:${type}`, controls: [
        'speed', 'effect intensity', 'custom 1', 'custom 2', 'custom 3',
    ].map(name => ({ name, value: 128, min: 0, max: 255 })) });
    for (const [type, name] of [
        ['HelioLakeEffect', 'effect intensity'],
        ['HelioColorTwinklesEffect', 'custom 1'],
        ['HelioColorTwinklesEffect', 'custom 2'],
        ['HelioColorTwinklesEffect', 'custom 3'],
        ['HelioGEQEffect', 'custom 2'],
    ]) {
        assert.equal(context.helioSliderRow(module(type), name), null, `${type}.${name} has no functional UI row`);
    }
    assert.equal(allocated, 0, 'hidden metadata creates no slider DOM');
    const row = context.helioSliderRow(module('HelioPhasedEffect'), 'speed');
    assert.ok(row);
    assert.ok(row.children.some(child => child.tagName === 'input' && child.type === 'range'));
});

function nativeRowsFixture(metadata = {}) {
    let allocated = 0;
    const element = tagName => ({ tagName, dataset: {}, style: {}, children: [], classList: { toggle() {} },
        append(...children) { this.children.push(...children); },
        appendChild(child) { this.children.push(child); }, addEventListener() {}, setAttribute() {} });
    const context = vm.createContext({
        document: { createElement: tagName => { ++allocated; return element(tagName); } },
        helioControl: (module, name) => module.controls.find(control => control.name === name),
        helioFactorySceneForType: type => type === 'NativeMetadataTest'
            ? { controlMetadata: metadata } : helioFactorySceneForType(type),
        helioSceneForPresetIndex, helioSceneControlMetadata,
        helioCurrentCount: () => 91,
        helioChoiceIndex: control => control.value || 0, helioOptionText: option => String(option),
        paletteCss: () => 'rgb(0,0,0)',
    });
    vm.runInContext(['helioControlSceneForEffect', 'appendHelioControlHelp', 'helioPrettyLabel', 'helioFieldName',
        'helioNumericRow', 'helioToggleRow', 'helioSelectRow', 'helioPaletteRow', 'helioNativeControlRow']
        .map(productFunction).join('\n'), context);
    return { context, allocated: () => allocated };
}

test('legacy rainbow segment controls use the physical length without clobbering an existing larger value', () => {
    const { context } = nativeRowsFixture();
    const stop = { name: 'segment stop', type: 'uint16', min: 0, max: 65535, value: 31 };
    const effect = { type: 'NebuliteRainbowEffect', controls: [stop] };
    const row = context.helioNativeControlRow(effect, stop);
    assert.equal(row.children.find(child => child.tagName === 'input').max, '91');
    stop.value = 120;
    const retained = context.helioNativeControlRow(effect, stop);
    assert.equal(retained.children.find(child => child.tagName === 'input').max, '120');
    assert.equal(stop.value, 120);
});

test('the real native Rings row displays the catalog background label and palette explanation', () => {
    const { context } = nativeRowsFixture();
    const control = { name: 'background brightness', type: 'uint8', value: 0, min: 0, max: 255 };
    const row = context.helioNativeControlRow({ name: '@preset:My rings', type: 'RingsEffect', controls: [control] }, control);
    assert.ok(row);
    assert.equal(row.children[0].textContent, 'Background brightness');
    const input = row.children.find(child => child.tagName === 'input');
    assert.equal(input.type, 'range');
    assert.equal(input.min, '0');
    assert.equal(input.max, '255');
    const help = row.children.filter(child => child.className === 'helio-control-help');
    assert.equal(help.length, 1);
    assert.equal(help[0].textContent, helioSceneControlDescription('ripple', control.name));
    assert.match(help[0].textContent, /palette.*Hue shift.*0.*black/);
});

test('native numeric, bool, select and palette rows share metadata labels and append their help exactly once', () => {
    for (const type of ['uint8', 'bool', 'select', 'palette']) {
        const { context } = nativeRowsFixture({ customThing: {
            label: 'Actual scene label', description: 'Actual scene explanation.', visible: true,
        } });
        const control = { name: 'customThing', type, value: 0, min: 0, max: 255, options: ['First'] };
        const row = context.helioNativeControlRow({ name: '@preset:Custom', type: 'NativeMetadataTest', controls: [control] }, control);
        assert.ok(row, type);
        assert.ok(row.children.some(child => child.textContent === 'Actual scene label'), type);
        const help = row.children.filter(child => child.className === 'helio-control-help');
        assert.equal(help.length, 1, type);
        assert.equal(help[0].textContent, 'Actual scene explanation.', type);
    }
});

test('native rows hide explicit false metadata and preserve visible fallbacks for unknown scenes and controls', () => {
    for (const type of ['uint8', 'bool', 'select', 'palette']) {
        const { context, allocated } = nativeRowsFixture({ customThing: { visible: false } });
        const control = { name: 'customThing', type, value: 0, options: [] };
        assert.equal(context.helioNativeControlRow({ type: 'NativeMetadataTest', controls: [control] }, control), null);
        assert.equal(allocated(), 0);
    }
    for (const [type, metadata] of [
        ['UnknownEffect', {}], ['NativeMetadataTest', {}],
        ['NativeMetadataTest', { customThing: { visible: null } }],
        ['NativeMetadataTest', { customThing: { visible: 0 } }],
    ]) {
        const { context } = nativeRowsFixture(metadata);
        const control = { name: 'customThing', type: 'uint8', value: 0, min: 0, max: 255 };
        const row = context.helioNativeControlRow({ type, controls: [control] }, control);
        assert.ok(row);
        assert.equal(row.children[0].textContent, 'Custom Thing');
        assert.equal(row.children.filter(child => child.className === 'helio-control-help').length, 0);
    }
});

test('a named native choice without loaded options stays visible and disabled without inventing an index', () => {
    const { context } = nativeRowsFixture();
    for (const type of ['select', 'palette']) {
        const control = { name: 'customer choice', type, value: 'Saved customer choice' };
        const row = context.helioNativeControlRow({ type: 'UnknownEffect', controls: [control] }, control);
        const holder = type === 'palette' ? row.children.find(child => child.tagName === 'label') : row;
        const select = holder.children.find(child => child.tagName === 'select');
        assert.equal(select.disabled, true);
        assert.equal(select.children[0].textContent, 'Saved customer choice');
        assert.equal(select.value, undefined);
        assert.equal(control.value, 'Saved customer choice');
    }
});
