import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';

const source = readFileSync(new URL('../../src/ui/nebulite-recording.js', import.meta.url), 'utf8');
const { decodeNebuliteRecording, recordingForPreset, attachRecordedPreview, prepareLegacyLook, recordingSha256 } = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);

test('HTTP fallback hash matches independent SHA-256 for boundary and recording sizes', () => {
    for (const length of [0,1,3,55,56,63,64,65,119,120,127,128,1350,4650,65537]) {
        const data = Uint8Array.from({length}, (_, i) => (i * 71 + 23) & 255);
        assert.equal(recordingSha256(data), createHash('sha256').update(data).digest('hex'), `length ${length}`);
    }
    assert.throws(() => recordingSha256(new Uint8Array(1048577)));
});

test('explicit RGB recordings keep frame and LED boundaries and wrap time', () => {
    const bytes = [1,2,3,4,5,6,7,8,9,10,11,12];
    const rec = decodeNebuliteRecording(Buffer.from(bytes).toString('base64'), 2);
    assert.equal(rec.frameCount, 2);
    assert.equal(rec.frameAt(0), 0); assert.equal(rec.frameAt(99), 0);
    assert.equal(rec.frameAt(100), 1); assert.equal(rec.frameAt(200), 0);
    assert.deepEqual(rec.sample(0, 1), [4,5,6]);
    assert.deepEqual(rec.sample(1, 0), [7,8,9]);
    assert.equal(rec.sample(2, 0), null); assert.equal(rec.sample(0, -1), null);
    assert.equal(rec.frameAt(-1), null); assert.equal(rec.frameAt(Infinity), null);
});

test('preparing an attachment is non-mutating and refuses invalid input', () => {
    const document = { Effects: { Layer: { speed: 7 } }, Drivers: { palette: 2 } };
    const output = attachRecordedPreview(document, 'AQID', 1, 100);
    assert.ok(output);
    assert.equal(document.$recordedPreview, undefined);
    assert.deepEqual(recordingForPreset(output).sample(0, 0), [1,2,3]);
    output.Effects.Layer.speed = 8;
    assert.equal(recordingForPreset(output), null);
    assert.equal(attachRecordedPreview(document, '!!!', 1), null);
    assert.equal(attachRecordedPreview({ Drivers: {} }, 'AQID', 1), null);
});

test('legacy review candidate requires its exact recording, catalogue, scene and typed control path', async () => {
    const catalog = '85872f1e2e02';
    const template = { $origin: { scene: 'nebulite-solid', catalog },
        $uiSchema: { effectType: 'NebuliteSolidEffect', effectPath: ['Effects','Layer','BLE Scene'],
            controls: ['red','green','blue','white','brightness','segment start','segment stop'].map(name =>
                ({ name, min: 0, max: name.startsWith('segment') ? 65535 : 255 })) },
        Effects: { Layer: { 'BLE Scene': { type: 'NebuliteSolidEffect', red: 2, green: 0, blue: 0, white: 0,
            brightness: 255, 'segment start': 0, 'segment stop': 0 } } } };
    const candidate = { status: 'review', scene: 'nebulite-solid', source_preset_sha256: 'a'.repeat(64),
        source_recording_sha256: '039058c6f2c0cb492c533b0a4d14ef77cc0f78abccced5287d84a1a2011cfb81',
        source_segments: [{ fx: 0, pal: 1, start: 0, stop: 1, col: [[123, 0, 0, 0]] }],
        controls: { red: 123, green: 0, blue: 0, white: 0, brightness: 255, 'segment start': 0, 'segment stop': 1 } };
    const doc = await prepareLegacyLook(template, candidate, catalog, 'AQID');
    assert.equal(template.Effects.Layer['BLE Scene'].red, 2);
    assert.equal(doc.Effects.Layer['BLE Scene'].red, 123);
    assert.deepEqual(recordingForPreset(doc).sample(0,0), [1,2,3]);
    assert.equal(doc.$recordedPreview.sourcePresetSha256, 'a'.repeat(64));
    const originalCrypto = Object.getOwnPropertyDescriptor(globalThis, 'crypto');
    try {
        Object.defineProperty(globalThis, 'crypto', { configurable: true, value: undefined });
        const httpDoc = await prepareLegacyLook(template, candidate, catalog, 'AQID');
        assert.deepEqual(httpDoc, doc);
        assert.equal(await prepareLegacyLook(template, candidate, catalog, 'BAUG'), null);
    } finally {
        if (originalCrypto) Object.defineProperty(globalThis, 'crypto', originalCrypto);
        else delete globalThis.crypto;
    }
    assert.equal(await prepareLegacyLook(template, candidate, '000000000000', 'AQID'), null);
    assert.equal(await prepareLegacyLook(template, { ...candidate, controls: { ...candidate.controls, red: 256 } }, catalog, 'AQID'), null);
    assert.equal(await prepareLegacyLook(template, { ...candidate, controls: { ...candidate.controls, red: 122 } }, catalog, 'AQID'), null);
    assert.equal(await prepareLegacyLook(template, { ...candidate, controls: { unexpected: 5 } }, catalog, 'AQID'), null);
    assert.equal(await prepareLegacyLook(template, { ...candidate, source_segments: [{ start: 0, stop: 2 }] }, catalog, 'AQID'), null);
    assert.equal(await prepareLegacyLook(template, candidate, catalog, 'BAUG'), null);
    assert.equal(await prepareLegacyLook(template, { ...candidate, source_recording_sha256: null }, catalog, 'AQID'), null);
    const bad = structuredClone(template); bad.$uiSchema.effectPath = ['Effects','__proto__'];
    assert.equal(await prepareLegacyLook(bad, candidate, catalog, 'AQID'), null);
});

test('legacy animation settings must match source speed, intensity and colors', async () => {
    const catalog = '85872f1e2e02', base = { 'segment start': 0, 'segment stop': 1 };
    const segment = { start: 0, stop: 1, sx: 61, ix: 102, col: [[1,2,3,0], [4,5,6,0]] };
    const cases = [
        ['nebulite-rainbow', 9, 11, { speed: 61, size: 102, ...base }, 'speed'],
        ['nebulite-washing', 113, 1, { speed: 61, 'effect intensity': 102, 'frame ms': 23, ...base }, 'effect intensity'],
        ['nebulite-snowflakes', 74, 36, { speed: 61, 'effect intensity': 102, 'source brightness': 255, 'follow master': true, 'frame ms': 24, ...base }, 'effect intensity'],
        ['nebulite-sine', 108, 1, { speed: 61, 'effect intensity': 102, 'secondary red': 4, 'secondary green': 5, 'secondary blue': 6, 'secondary white': 0, 'frame ms': 23, ...base }, 'secondary red'],
        ['nebulite-firefly', 106, 35, { speed: 61, 'effect intensity': 102, 'secondary red': 4, 'secondary green': 5, 'secondary blue': 6, 'secondary white': 0, 'frame ms': 23, ...base }, 'effect intensity'],
        ['nebulite-palette', 65, 3, { speed: 61, red: 1, green: 2, blue: 3, 'secondary red': 4, 'secondary green': 5, 'secondary blue': 6, 'frame ms': 23, ...base }, 'green'],
        ['nebulite-blink', 1, 3, { speed: 61, 'effect intensity': 102, red: 1, green: 2, blue: 3, 'secondary red': 4, 'secondary green': 5, 'secondary blue': 6, 'secondary white': 0, 'palette blend': 0, 'frame ms': 23, ...base }, 'blue'],
    ];
    for (const [scene, fx, pal, controls, altered] of cases) {
        const template = { $origin: { scene, catalog }, $uiSchema: { effectType: 'Effect', effectPath: ['Effects', 'Scene'],
            controls: Object.keys(controls).map(name => ({ name, type: typeof controls[name] === 'boolean' ? 'bool' : 'uint16', min: 0, max: 65535 })) },
            Effects: { Scene: { type: 'Effect', ...controls } } };
        const candidate = { status: 'review', scene, source_preset_sha256: 'a'.repeat(64),
            source_recording_sha256: '039058c6f2c0cb492c533b0a4d14ef77cc0f78abccced5287d84a1a2011cfb81',
            source_segments: [{ ...segment, fx, pal }], controls };
        assert.ok(await prepareLegacyLook(template, candidate, catalog, 'AQID'), scene);
        const changed = { ...candidate, controls: { ...controls, [altered]: controls[altered] + 1 } };
        assert.equal(await prepareLegacyLook(template, changed, catalog, 'AQID'), null, scene);
    }
});

test('Flow Stripe historical import checks source controls and segment length', async () => {
    const catalog = '85872f1e2e02', controls = {speed:61, 'effect intensity':102, 'frame ms':23, 'segment start':0, 'segment stop':2};
    const template = {$origin:{scene:'nebulite-flow-stripe',catalog}, $uiSchema:{effectType:'Effect',effectPath:['Effects','Scene'],
        controls:Object.keys(controls).map(name=>({name,type:'uint16',min:0,max:65535}))}, Effects:{Scene:{type:'Effect',...controls}}};
    const candidate = {status:'review',scene:'nebulite-flow-stripe',source_preset_sha256:'a'.repeat(64),
        source_recording_sha256:'7192385c3c0605de55bb9476ce1d90748190ecb32a8eed7f5207b30cf6a1fe89',
        source_segments:[{start:0,stop:2,fx:179,pal:74,sx:61,ix:102,col:[[0,8,255,0]]}],controls};
    assert.ok(await prepareLegacyLook(template,candidate,catalog,'AQIDBAUG'));
    assert.equal(await prepareLegacyLook(template,{...candidate,controls:{...controls,'effect intensity':103}},catalog,'AQIDBAUG'),null);
    const short = {...candidate,source_segments:[{...candidate.source_segments[0],stop:1}],controls:{...controls,'segment stop':1}};
    assert.equal(await prepareLegacyLook(template,short,catalog,'AQIDBAUG'),null);
});

test('Plasmoid historical import keeps source sensitivity and does not invent audio input', async () => {
    const catalog = '85872f1e2e02', controls = {speed:41,'effect intensity':112,'audio floor':0,'simulate if no source':true,
        'secondary red':0,'secondary green':0,'secondary blue':0,'secondary white':0,
        'frame ms':23,'segment start':0,'segment stop':2};
    const template = {$origin:{scene:'nebulite-plasmoid',catalog}, $uiSchema:{effectType:'Effect',effectPath:['Effects','Scene'],
        controls:Object.keys(controls).map(name=>name==='simulate if no source'?{name,type:'bool'}:{name,type:'uint16',min:0,max:65535})}, Effects:{Scene:{type:'Effect',...controls}}};
    const candidate = {status:'review',scene:'nebulite-plasmoid',source_preset_sha256:'a'.repeat(64),
        source_recording_sha256:'7192385c3c0605de55bb9476ce1d90748190ecb32a8eed7f5207b30cf6a1fe89',
        source_segments:[{start:0,stop:2,fx:133,pal:74,sx:41,ix:112,col:[[102,255,138,0],[0,0,0,0]]}],controls};
    assert.ok(await prepareLegacyLook(template,candidate,catalog,'AQIDBAUG'));
    assert.equal(await prepareLegacyLook(template,{...candidate,controls:{...controls,'audio floor':1}},catalog,'AQIDBAUG'),null);
    assert.equal(await prepareLegacyLook(template,{...candidate,controls:{...controls,'effect intensity':113}},catalog,'AQIDBAUG'),null);
});

test('a historical recording is valid only for the exact bound look', () => {
    const doc = { Effects: { Layer: { speed: 7 } }, Drivers: { palette: 2 } };
    doc.$recordedPreview = { version: 1, encoding: 'rgb8-base64', ledCount: 1, frameMs: 100,
        data: 'AQID', forLook: JSON.stringify({ Effects: doc.Effects, Drivers: doc.Drivers }) };
    const first = recordingForPreset(doc);
    assert.deepEqual(first.sample(0, 0), [1,2,3]);
    assert.equal(recordingForPreset(doc), first);
    const owner = {};
    const cachedCopy = recordingForPreset(structuredClone(doc), owner);
    assert.equal(recordingForPreset(structuredClone(doc), owner), cachedCopy);
    doc.Effects.Layer.speed = 8;
    assert.equal(recordingForPreset(doc), null);
    doc.Effects.Layer.speed = 7;
    assert.equal(recordingForPreset(doc), first);
    doc.$recordedPreview.data = 'BAUG';
    assert.deepEqual(recordingForPreset(doc).sample(0, 0), [4,5,6]);
    doc.$recordedPreview.encoding = 'rgbw';
    assert.equal(recordingForPreset(doc), null);
});

test('malformed recordings and unsupported dimensions never make a partial frame', () => {
    assert.equal(decodeNebuliteRecording('not base64!', 1), null);
    assert.equal(decodeNebuliteRecording(Buffer.from([1,2,3,4]).toString('base64'), 1), null);
    assert.equal(decodeNebuliteRecording('AAAA', 0), null);
    assert.equal(decodeNebuliteRecording('AAAA', 513), null);
    assert.equal(decodeNebuliteRecording('AAAA', 1, 0), null);
    assert.equal(decodeNebuliteRecording('', 1), null);
});
