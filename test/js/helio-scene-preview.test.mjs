import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';

const moduleUrl = source => `data:text/javascript;base64,${Buffer.from(source).toString('base64')}`;
const data = readFileSync(new URL('../../src/ui/helio-scene-data.js', import.meta.url), 'utf8');
const scenes = readFileSync(new URL('../../src/ui/helio-scenes.js', import.meta.url), 'utf8')
    .replaceAll('"./helio-scene-data.js"', JSON.stringify(moduleUrl(data)));
const source = readFileSync(new URL('../../src/ui/helio-preview.js', import.meta.url), 'utf8')
    .replace('"./helio-scenes.js"', JSON.stringify(moduleUrl(scenes)));
const { helioScenePreviewColor, helioPresetPreviewColor, drawNebuliteStripPreview, drawNebuliteOutputPreview } = await import(moduleUrl(source));

test('Noisefire preview matches 120 native frames with identical simulated audio', () => {
    const effect = {type:'NebuliteNoisefireEffect', controls:[{name:'segment stop',value:31}]};
    const bytes = [];
    for (let frame=0; frame<120; ++frame)
        for (let pixel=0; pixel<31; ++pixel)
            bytes.push(...helioScenePreviewColor(effect, [], (frame*23+1)/1000, (pixel+.5)/31, .5,
                {level:128/255}, {ledCount:31}));
    assert.equal(createHash('sha256').update(Buffer.from(bytes)).digest('hex'),
        '78522fd26114451bf46dc5be9f85e6070659baac8e83a48ca37b592c3ae0ee46');
});

test('Noisefire simulation only replaces an absent source when selected', () => {
    const effect={type:'NebuliteNoisefireEffect',controls:[{name:'simulate if no source',value:true}]};
    const sample=(t,present)=>Array.from({length:12},(_,pixel)=>helioScenePreviewColor(effect,[],t,(pixel+.5)/12,.5,
        {level:0},{ledCount:12,audioSourcePresent:present}));
    assert.ok(sample(.023,false).some(pixel=>pixel.some(Boolean)));
    assert.ok(sample(.046,true).every(pixel=>pixel.every(v=>v===0)));
});

test('Noisefire no-source simulation matches native fallback over 120 frames', () => {
    const effect={type:'NebuliteNoisefireEffect',controls:[{name:'segment stop',value:31},{name:'simulate if no source',value:true}]};
    const bytes=[];
    for(let f=0;f<120;f++)for(let i=0;i<31;i++)
        bytes.push(...helioScenePreviewColor(effect,[],(f*23+1)/1000,(i+.5)/31,.5,{level:0},{ledCount:31,audioSourcePresent:false}));
    assert.equal(createHash('sha256').update(Buffer.from(bytes)).digest('hex'),
        '0cb8273d973fc27045bcb23467ef8659d990abf2a0781eaaa5dec0e5e1c815de');
});

test('Plasmoid preview reproduces native source frames with the same simulated input', () => {
    const effect = {type:'NebulitePlasmoidEffect', controls:[{name:'segment stop',value:31}]};
    const bytes = [];
    for (let frame=0; frame<120; ++frame)
        for (let pixel=0; pixel<31; ++pixel)
            bytes.push(...helioScenePreviewColor(effect, [], (frame*23+1)/1000, (pixel+.5)/31, .5,
                {level:128/255}, {ledCount:31, rgbOnly:true}));
    assert.equal(createHash('sha256').update(Buffer.from(bytes)).digest('hex'),
        '0fa149d76b3280b5260387e99dc8d8ecbbb4b90d6e78cb2ea2b851f052590426');
});

test('Plasmoid preview holds frames and silence does not invent microphone input', () => {
    const effect = {type:'NebulitePlasmoidEffect', controls:[{name:'segment start',value:2},{name:'segment stop',value:10}]};
    const sample = (t,level) => Array.from({length:12},(_,i) =>
        helioScenePreviewColor(effect, [], t, (i+.5)/12, .5, {level}, {ledCount:12}));
    assert.deepEqual(sample(0,0), Array.from({length:12},()=>[0,0,0]));
    const lit = sample(.023,1);
    assert.ok(lit.slice(2,10).some(pixel => pixel.some(Boolean)));
    assert.deepEqual(lit.slice(0,2), [[0,0,0],[0,0,0]]);
    assert.deepEqual(lit.slice(10), [[0,0,0],[0,0,0]]);
    assert.deepEqual(sample(.024,0), lit);
});

test('Flow Stripe preview holds its frame and stays inside the selected segment', () => {
    const effect = { type: 'NebuliteFlowStripeEffect', controls: [
        {name:'segment start',value:2}, {name:'segment stop',value:10}, {name:'frame ms',value:23},
    ] };
    const frame = t => Array.from({length:12}, (_,i) => helioScenePreviewColor(effect, [], t, (i+.5)/12, .5, undefined, {ledCount:12}));
    const first = frame(1);
    assert.deepEqual(first.slice(0,2), [[0,0,0],[0,0,0]]);
    assert.deepEqual(first.slice(10), [[0,0,0],[0,0,0]]);
    assert.ok(first.slice(2,10).every(pixel => pixel.some(Boolean)));
    assert.deepEqual(frame(1.001), first);
    effect.controls[1].value = 3;
    assert.deepEqual(frame(1.023), Array.from({length:12}, () => [0,0,0]));
});

test('Flow Stripe preview matches native pre-driver RGB across 120 frames', () => {
    const effect = {type:'NebuliteFlowStripeEffect', controls:[{name:'segment stop',value:31}]};
    const bytes = [];
    for (let frame=0; frame<120; ++frame)
        for (let pixel=0; pixel<31; ++pixel)
            bytes.push(...helioScenePreviewColor(effect, [], (frame*23+1)/1000, (pixel+.5)/31, .5, undefined, {ledCount:31}));
    assert.equal(createHash('sha256').update(Buffer.from(bytes)).digest('hex'),
        'f9ef43b3433ddfc5aa5eb22833836593177c7468741118f8ab7af6fff8911478');
});

test('snowflakes preview follows its segment and holds frames between updates', () => {
    const effect = { type: 'NebuliteSnowflakesEffect', controls: [
        {name:'segment start', value:2}, {name:'segment stop', value:10},
        {name:'effect intensity', value:255}, {name:'frame ms', value:24},
    ] };
    const sample = (i,t) => helioScenePreviewColor(effect, [], t, (i+.5)/12, .5, undefined, {ledCount:12});
    const frame = t => Array.from({length:12}, (_,i) => sample(i,t));
    let lit = false;
    for (let n=0;n<10;n++) {
        const pixels = frame((n*24+1)/1000);
        assert.deepEqual(pixels.slice(0,2), [[0,0,0],[0,0,0]]);
        assert.deepEqual(pixels.slice(10), [[0,0,0],[0,0,0]]);
        lit ||= pixels.slice(2,10).some(c => c.some(Boolean));
    }
    assert.ok(lit);
    assert.deepEqual(frame(.218), frame(.219));
});

test('snowflakes preview matches 120 native source frames with the same seed', () => {
    const effect = {type:'NebuliteSnowflakesEffect', controls:[
        {name:'segment stop',value:31}, {name:'speed',value:35}, {name:'effect intensity',value:197},
    ]};
    const bytes = [];
    for (let frame=0; frame<120; ++frame)
        for (let pixel=0; pixel<31; ++pixel)
            bytes.push(...helioScenePreviewColor(effect, [], (frame*24+1)/1000, (pixel+.5)/31, .5, undefined, {ledCount:31}));
    assert.equal(createHash('sha256').update(Buffer.from(bytes)).digest('hex'),
        '26585dc892ca0b7e58c3bbabe62d8cef9ac7d909e0fa5c5fccd70750cd1a4245');
});

test('snowflakes preview follows the current master or its manual reference', () => {
    const automatic = {type:'NebuliteSnowflakesEffect', controls:[]};
    const manual = {type:'NebuliteSnowflakesEffect', controls:[{name:'follow master',value:false}]};
    const render = (effect,t) => Array.from({length:31}, (_,i) =>
        helioScenePreviewColor(effect, [], t, (i+.5)/31, .5, undefined, {ledCount:31,getMasterBrightness:()=>5}));
    let a, b;
    for (let frame=0;frame<25;frame++) { a=render(automatic,(frame*24+1)/1000); b=render(manual,(frame*24+1)/1000); }
    assert.notDeepEqual(a,b);
});

test('firefly preview keeps the same sequence when a segment moves and after repeated frames', () => {
    const effect = start => ({ type: 'NebuliteFireflyEffect', controls: [
        { name: 'speed', value: 55 }, { name: 'effect intensity', value: 255 },
        { name: 'segment start', value: start }, { name: 'segment stop', value: start + 9 },
    ] });
    const a = effect(0), b = effect(12);
    const sample = (scene, pixel, time) => helioScenePreviewColor(scene, [], time, (pixel + 0.5) / 31, 0.5, undefined, { ledCount: 31, rgbOnly: true });
    for (const time of [0, 0.023, 1.012, 0])
        for (let pixel = 0; pixel < 9; ++pixel)
            assert.deepEqual(sample(a, pixel, time), sample(b, pixel + 12, time));
    assert.deepEqual(sample(b, 11, 0), [0, 0, 0]);
});

test('legacy rainbow preview follows the physical segment and fixed FastLED palette', () => {
    const effect = { type: 'NebuliteRainbowEffect', controls: [
        { name: 'speed', value: 128 }, { name: 'size', value: 128 },
        { name: 'segment start', value: 0 }, { name: 'segment stop', value: 31 },
    ] };
    const color = i => helioScenePreviewColor(effect, [], 0, (i + 0.5) / 91, 0.5, undefined, { ledCount: 91 });
    assert.deepEqual(color(0), [255, 0, 0]);
    assert.notDeepEqual(color(30), [0, 0, 0]);
    assert.deepEqual(color(31), [0, 0, 0]);
    assert.deepEqual(color(90), [0, 0, 0]);
});

test('wearable washing preview animates its selected segment and resets on a time rewind', () => {
    const effect = { type: 'NebuliteWashingEffect', controls: [
        { name: 'segment start', value: 3 }, { name: 'segment stop', value: 31 },
    ] };
    const color = (pixel, time) => helioScenePreviewColor(effect, [], time,
        (pixel + 0.5) / 91, 0.5, undefined, { ledCount: 91 });
    assert.deepEqual(color(2, 0), [0, 0, 0]);
    const first = color(10, 0);
    assert.notDeepEqual(first, [0, 0, 0]);
    assert.deepEqual(color(31, 0), [0, 0, 0]);
    assert.deepEqual(color(10, 0), first);
    color(10, 6);
    assert.deepEqual(color(10, 0), first);
});

test('wearable sine preview keeps its RGBW background and physical segment', () => {
    const effect = { type: 'NebuliteSineEffect', controls: [
        {name:'speed',value:0}, {name:'effect intensity',value:0},
        {name:'secondary red',value:42}, {name:'secondary white',value:73},
        {name:'segment start',value:3}, {name:'segment stop',value:9},
    ] };
    const color = (pixel, rgbOnly) => helioScenePreviewColor(effect, [], 0, (pixel+.5)/12, .5,
        undefined, {ledCount:12,rgbOnly});
    assert.deepEqual(color(2, true), [0,0,0]);
    assert.deepEqual(color(3, true), [42,0,0]);
    assert.notDeepEqual(color(3, false), [42,0,0]);
    assert.deepEqual(color(9, true), [0,0,0]);
});

test('wearable sine preview holds its whole frame between reference updates', () => {
    const effect = { type:'NebuliteSineEffect', controls:[
        {name:'speed',value:128}, {name:'effect intensity',value:128}, {name:'frame ms',value:23},
    ] };
    const sample = t => helioScenePreviewColor(effect, [], t, 10.5/31, .5, undefined, {ledCount:31});
    sample(0);
    const frame = sample(.023);
    assert.deepEqual(sample(.032), frame);
});

test('wearable sine preview matches native RGB frames across a palette transition', () => {
    // Captured from NebuliteSineEffect on a 31-light desktop Layer with its default seed.
    const effect = {type:'NebuliteSineEffect', controls:[]};
    const lines = [0,23,32,46,5023,5046].map(ms => JSON.stringify(Array.from({length:31}, (_, i) =>
        helioScenePreviewColor(effect, [], ms/1000, (i+.5)/31, .5, undefined, {ledCount:31,rgbOnly:true}))));
    const hash = createHash('sha256').update(lines.join('\n')+'\n').digest('hex');
    assert.equal(hash, '5df9ed9d7c2a46bf81982fe86bd5060dc0fef712cf1dd19168ccb0a45a3a41e5');
});

test('wearable two-color palette preview respects its segment and holds frames', () => {
    const effect={type:'NebulitePaletteEffect',controls:[
        {name:'speed',value:128},{name:'segment start',value:2},{name:'segment stop',value:9},
    ]};
    const color=(i,t)=>helioScenePreviewColor(effect,[],t,(i+.5)/12,.5,undefined,{ledCount:12});
    assert.deepEqual(color(1,0),[0,0,0]);
    const first=color(2,0); assert.deepEqual(color(2,.001),first);
    assert.notDeepEqual(color(2,.023),first);
    assert.deepEqual(color(9,.023),[0,0,0]);
});

test('wearable two-color palette preview matches native frames', () => {
    const effect={type:'NebulitePaletteEffect',controls:[]};
    const lines=[0,23,32,46,5023,5046].map(ms=>JSON.stringify(Array.from({length:31},(_,i)=>
        helioScenePreviewColor(effect,[],ms/1000,(i+.5)/31,.5,undefined,{ledCount:31}))));
    assert.equal(createHash('sha256').update(lines.join('\n')+'\n').digest('hex'),
        'd63f5574f1bf425b24932c8c9548b43740e4c1337a7a198824ef43077fc46902');
});

test('wearable blink preview respects the physical segment and nominal timing', () => {
    const effect = { type: 'NebuliteBlinkEffect', controls: [
        { name: 'segment start', value: 1 }, { name: 'segment stop', value: 10 },
        { name: 'secondary red', value: 20 }, { name: 'secondary white', value: 80 },
    ] };
    const color = (pixel, t, rgbOnly = true) => helioScenePreviewColor(effect, [], t,
        (pixel + 0.5) / 12, 0.5, undefined, { ledCount: 12, rgbOnly });
    assert.deepEqual(color(0, 0), [0, 0, 0]);
    assert.deepEqual(color(1, 0), [0, 0, 255]);
    assert.deepEqual(color(10, 0), [0, 0, 0]);
    assert.deepEqual(color(1, 0.274), [20, 0, 0]);
    assert.notDeepEqual(color(1, 0.274, false), [20, 0, 0]);
    assert.deepEqual(color(1, 0.546), [0, 0, 255]);
});

test('wearable solid preview keeps RGBW, brightness and segment independent', () => {
    const effect = {type: 'NebuliteSolidEffect', controls: [
        {name:'red',value:0}, {name:'green',value:8}, {name:'blue',value:255},
        {name:'white',value:40}, {name:'brightness',value:128},
        {name:'segment start',value:0}, {name:'segment stop',value:31},
    ]};
    const sample = (i, rgbOnly) => helioScenePreviewColor(effect, [], 0, (i + .5) / 91, .5, undefined, {ledCount:91,rgbOnly});
    assert.deepEqual(sample(0, true), [0,4,128]);
    assert.deepEqual(sample(30, true), [0,4,128]);
    assert.deepEqual(sample(31, true), [0,0,0]);
    assert.notDeepEqual(sample(0, false), sample(0, true));
});

test('jacket output diagram samples all 90 RGB positions and skips the independent white slot', () => {
    const samples = [], rectangles = [], attributes = {};
    const canvas = { width: 384, height: 384, dataset: {}, getContext: () => ({
        set fillStyle(_value) {}, fillRect(...args) { rectangles.push(args); },
    }), setAttribute: (key, value) => { attributes[key] = value; } };
    const outputs = [{ start: 0, count: 30 }, { start: 31, count: 30 }, { start: 61, count: 30 }];
    assert.equal(drawNebuliteOutputPreview(canvas, 2, x => { samples.push(x); return [255, 0, 0]; }, 91, outputs), true);
    assert.equal(samples.length, 90);
    assert.equal(samples[29], 29.5 / 91);
    assert.equal(samples[30], 31.5 / 91);
    assert.equal(samples[89], 90.5 / 91);
    assert.equal(rectangles.length, 91);
    assert.match(attributes['aria-label'], /garment geometry and separate white output not shown/);
    assert.equal(drawNebuliteOutputPreview(canvas, 0, () => [], 91, [{ start: 90, count: 2 }]), false);
});

test('NEBULITE scene preview samples the ordered strip without claiming garment geometry', () => {
    const rectangles = [], samples = [], attributes = {};
    const context = { fillStyle: '', fillRect(...args) { rectangles.push({ color: this.fillStyle, args }); } };
    const canvas = { width: 100, height: 100, dataset: {}, getContext: () => context,
        setAttribute: (key, value) => { attributes[key] = value; } };
    assert.equal(drawNebuliteStripPreview(canvas, 2, (x, y, time) => {
        samples.push([x, y, time]); return [x * 255, 0, 0];
    }, 4), true);
    assert.deepEqual(samples, [[0.125, 0.5, 2], [0.375, 0.5, 2], [0.625, 0.5, 2], [0.875, 0.5, 2]]);
    assert.equal(rectangles.length, 5); // background plus four ordered pixels
    assert.equal(canvas.dataset.previewRenderer, 'nebulite-strip-simulation');
    assert.match(attributes['aria-label'], /Approximate.*linear RGB LED strip/);
    assert.doesNotMatch(attributes['aria-label'], /white output/);
    assert.equal(drawNebuliteStripPreview({ getContext: () => null }, 0, () => [], 4), false);
});

test('white PWM slot is excluded while RGB sampling preserves logical positions', () => {
    const samples = [], attributes = {}, canvas = { width: 100, height: 50, dataset: {},
        getContext: () => ({ fillRect() {}, set fillStyle(_value) {} }),
        setAttribute: (key, value) => { attributes[key] = value; } };
    drawNebuliteStripPreview(canvas, 0, x => { samples.push(x); return [0, 0, 0]; }, 4, [1]);
    assert.deepEqual(samples, [0.125, 0.625, 0.875]);
    assert.match(attributes['aria-label'], /white output not shown/);
});

test('garment RGB strip preview does not add the independent white output to RGB pixels', () => {
    const rgb = { red: 40, green: 50, blue: 60, white: 0, output: 255 };
    const withWhite = { ...rgb, white: 200 };
    const preview = controls => helioPresetPreviewColor(0, controls, [[255, 255, 255]], 0, 0.2, 0.5, undefined,
        { rgbOnly: true, ledCount: 9 });
    assert.deepEqual(preview(withWhite), preview(rgb));
    assert.notDeepEqual(helioPresetPreviewColor(0, withWhite, [[255, 255, 255]], 0, 0.2, 0.5), preview(withWhite));
});

function sample(background, { palette = [[255, 0, 0]], hue = 0, x = 0, y = 0, t = 0 } = {}) {
    const controls = [
        { name: 'count', value: 1 }, { name: 'thickness', value: 1 },
        { name: 'hue_shift', value: hue }, { name: 'speed', value: 1 },
    ];
    if (background !== undefined) controls.push({ name: 'background brightness', value: background });
    return helioScenePreviewColor({ type: 'RingsEffect', controls }, palette, t, x, y);
}

test('Rings procedural preview keeps an omitted or zero background black between rings', () => {
    assert.deepEqual(sample(), [0, 0, 0]);
    for (const t of [0, 0.1, 0.7, 2]) {
        for (let y = 0; y <= 1; y += 0.2) for (let x = 0; x <= 1; x += 0.2)
            assert.deepEqual(sample(undefined, { x, y, t }), sample(0, { x, y, t }));
    }
});

test('Rings procedural background uses the selected palette hue and brightness without adding fixed white', () => {
    const palette = [[255, 0, 0], [0, 255, 0], [0, 0, 255], [255, 0, 0]];
    assert.deepEqual(sample(64, { palette }), [64, 0, 0]);
    assert.deepEqual(sample(64, { palette, hue: 64 }), [0, 64, 0]);
    assert.deepEqual(sample(255, { palette: [[0, 0, 0]] }), [0, 0, 0]);
    assert.deepEqual(sample(-20), sample(0));
    assert.deepEqual(sample(400), sample(255));
});

test('Rings procedural preview adds its background to the existing rings and clamps channel overlap', () => {
    let lit = false;
    for (let y = 0; y <= 1; y += 0.02) for (let x = 0; x <= 1; x += 0.02) {
        const before = sample(0, { x, y }), after = sample(32, { x, y });
        lit ||= before[0] > 0;
        assert.deepEqual(after, [Math.min(255, before[0] + 32), 0, 0]);
    }
    assert.equal(lit, true);
});
