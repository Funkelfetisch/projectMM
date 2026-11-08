import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const app = readFileSync(new URL('../../src/ui/helio-app.js', import.meta.url), 'utf8');
const orchestration = readFileSync(new URL('../../src/ui/helio-orchestration.js', import.meta.url), 'utf8');
const { planHelioControlDocument, planHelioEffectActivation } = await import(
    `data:text/javascript;base64,${Buffer.from(orchestration).toString('base64')}`);

// These behaviours live in the product script; the generic app-source fixture
// intentionally exercises app.js and cannot cover this dashboard.
function functionSource(name) {
    const match = new RegExp(`^(?:async )?function ${name}\\(`, 'm').exec(app);
    assert.ok(match, `${name} exists in helio-app.js`);
    const end = app.indexOf('\n}', match.index);
    assert.ok(end > match.index, `${name} has a top-level closing brace`);
    return app.slice(match.index, end + 2);
}

function contextFor(names, globals = {}) {
    const context = vm.createContext({ console: { warn() {} }, state: null, wsUnloading: false,
        initialFirmwareModule: null, earlyHelioHome: null, deviceFirmwareInfo: () => null,
        isHelioFirmware: () => false, isNebuliteFirmware: () => false, renderEarlyHelioHome() {},
        helioMetadataGeneration: 0, helioValueRevision: 0,
        helioValueChanges: new Map(), helioMetadataReady: new Set(), helioMetadataReads: new Map(),
        helioNeedsModuleMetadata: () => false, mergeHelioModuleMetadata() {}, previewWs: null, connectPreviewSocket() {},
        productProjection: () => 'strip', productProjectionPoints: () => null,
        drawJacketPreview: () => false, whenJacketPreviewReady() {},
        takeInitialPresetIndexResponse: () => null,
        deferHelioTraceUntilCards() {},
        ...globals });
    vm.runInContext(names.map(functionSource).join('\n'), context);
    return context;
}

function deferred() {
    let resolve, reject;
    const promise = new Promise((done, fail) => { resolve = done; reject = fail; });
    return { promise, resolve, reject };
}

async function until(predicate) {
    for (let i = 0; i < 30 && !predicate(); i++) await Promise.resolve();
    assert.ok(predicate(), 'the asynchronous checkpoint was reached');
}

const plain = value => JSON.parse(JSON.stringify(value));
const scene = id => ({ id, type: `${id}Effect`, label: `Scene ${id}`, controls: { speed: 90 } });

function tree() {
    const old = { name: 'Old', type: 'OldEffect', enabled: true, controls: [] };
    const target = { name: 'Twinkles', type: 'TwinklesEffect', enabled: false,
        controls: [{ name: 'speed', value: 20 }] };
    return { old, target, modules: [{ name: 'Effects', children: [{ name: 'Layer', children: [old, target] }] }] };
}

test('the jacket illustration requires matching live setup and its 30 mirrored lights', async () => {
    const load = file => import('data:text/javascript;base64,' + Buffer.from(readFileSync(new URL(file, import.meta.url), 'utf8')).toString('base64'));
    const { NEBULITE_SETUP_PROFILES } = await load('../../src/ui/nebulite-profile-data.js');
    const { productProfileMatches } = await load('../../src/ui/nebulite-setup.js');
    const profile = NEBULITE_SETUP_PROFILES.find(item => item.name === 'NEBULITE Jacket S3');
    const system = { name: 'System', controls: [{ name: 'deviceModel', value: profile.name }] };
    const modules = [system, ...['Drivers', 'Layouts'].map(name => ({ name,
        children: Object.entries(profile.state[name]).filter(([, item]) => item?.type).map(([name, item]) => ({
            name, type: item.type, role: 'driver', controls: Object.entries(item)
                .filter(([key]) => !['type', '$patch'].includes(key)).map(([name, value]) => ({ name, value })),
        })) }))];
    const calls = [];
    const context = contextFor(['drawHelioCardTracePreview'], { state: { modules }, NEBULITE_SETUP_PROFILES,
        isNebuliteFirmware: () => true, findModule: () => system, controlValue: module => module.controls[0].value,
        productProfileMatches, productProjection: () => 'strip', productProjectionPoints: () => ({ points: [], width: 1, height: 1 }),
        requestNebulitePreviewMetadata() {}, setHelioPreviewKind() {}, whenJacketPreviewReady() {}, refreshHistoricalCroptopPreviews() {},
        drawJacketPreview: () => { calls.push('jacket'); return true; },
        drawNebuliteStripPreview: () => calls.push('strip') });
    context.drawHelioCardTracePreview({}, 0, () => [], 30);
    // A string moved to another pad is still the jacket; a different light count is custom wiring.
    modules[1].children[0].controls.find(item => item.name === 'pins').value = '34';
    context.drawHelioCardTracePreview({}, 0, () => [], 30);
    modules[1].children[0].controls.find(item => item.name === 'ledsPerPin').value = '31';
    context.drawHelioCardTracePreview({}, 0, () => [], 30);
    assert.deepEqual(calls, ['jacket', 'jacket', 'strip']);
});

test('historical crop-top geometry requires matching live setup and complete logical length', async () => {
    const load = file => import('data:text/javascript;base64,' + Buffer.from(readFileSync(new URL(file, import.meta.url), 'utf8')).toString('base64'));
    const { NEBULITE_SETUP_PROFILES } = await load('../../src/ui/nebulite-profile-data.js');
    const { productProfileMatches } = await load('../../src/ui/nebulite-setup.js');
    const profile = NEBULITE_SETUP_PROFILES.find(item => item.name === 'NEBULITE Croptop S3');
    const system = { name: 'System', controls: [{ name: 'deviceModel', value: profile.name }] };
    const modules = [system, ...['Drivers', 'Layouts'].map(name => ({ name,
        children: Object.entries(profile.state[name]).filter(([, item]) => item?.type).map(([name, item]) => ({
            name, type: item.type, role: 'driver', controls: Object.entries(item)
                .filter(([key]) => !['type', '$patch'].includes(key)).map(([name, value]) => ({ name, value })),
        })) }))];
    const calls = [];
    const context = contextFor(['drawHelioCardTracePreview'], { state: { modules }, NEBULITE_SETUP_PROFILES,
        isNebuliteFirmware: () => true, findModule: () => system, controlValue: module => module.controls[0].value,
        productProfileMatches, requestNebulitePreviewMetadata() {}, setHelioPreviewKind() {}, drawHistoricalCroptop: () => { calls.push('historical'); return true; },
        drawNebuliteStripPreview: () => calls.push('strip') });
    context.drawHelioCardTracePreview({}, 0, () => [], 8);
    assert.deepEqual(calls, ['historical']);
    context.drawHelioCardTracePreview({}, 0, () => [], 9);
    // The bench case: the strip moved to the pad at GPIO34 still shows the crop-top.
    modules[1].children[0].controls.find(item => item.name === 'pins').value = '34';
    context.drawHelioCardTracePreview({}, 0, () => [], 8);
    modules[1].children[0].controls.find(item => item.name === 'ledsPerPin').value = '9';
    context.drawHelioCardTracePreview({}, 0, () => [], 8);
    assert.deepEqual(calls, ['historical', 'strip', 'historical', 'strip']);
});

test('a 2D scene is sampled at each output cell before the garment simulator uses strip order', async () => {
    const load = file => import('data:text/javascript;base64,' + Buffer.from(readFileSync(new URL(file, import.meta.url), 'utf8')).toString('base64'));
    const { NEBULITE_SETUP_PROFILES } = await load('../../src/ui/nebulite-profile-data.js');
    const { productProfileMatches, productProjection, productProjectionPoints, productProjectionProfile } = await load('../../src/ui/nebulite-setup.js');
    const base = NEBULITE_SETUP_PROFILES.find(item => item.name === 'NEBULITE Croptop S3');
    const profile = productProjectionProfile(base, 'virtual2d');
    const system = { name: 'System', controls: [{ name: 'deviceModel', value: profile.name }] };
    const modules = [system, ...['Drivers', 'Layouts'].map(name => ({ name,
        children: Object.entries(profile.state[name]).filter(([, item]) => item?.type).map(([name, item]) => ({
            name, type: item.type, role: 'driver', controls: Object.entries(item)
                .filter(([key]) => !['type', '$patch'].includes(key)).map(([name, value]) => ({ name, value })),
        })) }))];
    const points = [];
    const context = contextFor(['drawHelioCardTracePreview'], { state:{modules}, NEBULITE_SETUP_PROFILES,
        isNebuliteFirmware:()=>true, findModule:()=>system, controlValue:module=>module.controls[0].value,
        productProfileMatches, productProjection, productProjectionPoints, requestNebulitePreviewMetadata(){}, setHelioPreviewKind(){},
        drawHistoricalCroptop:(_canvas,t,sample)=>{ for(let i=0;i<8;i++)sample((i+.5)/8,.5,t);return true; } });
    context.drawHelioCardTracePreview({}, 0, (x,y)=>{points.push([x,y]);return [0,0,0];}, 8, {effect:{type:'PlasmaEffect'}});
    assert.equal(points.length,8);
    const expected = productProjectionPoints(base);
    assert.deepEqual(points.map(([x,y])=>[Math.round(x*expected.width-.5),Math.round(y*expected.height-.5)]),expected.points);
});

test('classic crop-top samples all eight configured 2D positions in output order', async () => {
    const load = file => import('data:text/javascript;base64,' + Buffer.from(readFileSync(new URL(file, import.meta.url), 'utf8')).toString('base64'));
    const { NEBULITE_SETUP_PROFILES } = await load('../../src/ui/nebulite-profile-data.js');
    const { productProfileMatches, productProjection, productProjectionPoints, productProjectionProfile } = await load('../../src/ui/nebulite-setup.js');
    const base = NEBULITE_SETUP_PROFILES.find(item => item.name === 'NEBULITE Croptop ESP32');
    const profile = productProjectionProfile(base, 'virtual2d');
    const system = { name: 'System', controls: [{ name: 'deviceModel', value: profile.name }] };
    const modules = [system, ...['Drivers', 'Layouts'].map(name => ({ name,
        children: Object.entries(profile.state[name]).filter(([, item]) => item?.type).map(([name, item]) => ({
            name, type: item.type, role: 'driver', controls: Object.entries(item)
                .filter(([key]) => !['type', '$patch'].includes(key)).map(([name, value]) => ({ name, value })),
        })) }))];
    const points = [];
    const context = contextFor(['drawHelioCardTracePreview'], { state: { modules }, NEBULITE_SETUP_PROFILES,
        isNebuliteFirmware:()=>true, findModule:()=>system, controlValue:module=>module.controls[0].value,
        productProfileMatches, productProjection, productProjectionPoints, requestNebulitePreviewMetadata(){}, setHelioPreviewKind(){},
        drawHistoricalCroptop:(_canvas,t,sample)=>{ for(let i=0;i<8;i++)sample((i+.5)/8,.5,t);return true; } });
    context.drawHelioCardTracePreview({}, 0, (x,y)=>{points.push([x,y]);return [0,0,0];}, 8, {effect:{type:'PlasmaEffect'}});
    const expected = productProjectionPoints(base);
    assert.deepEqual(points.map(([x,y])=>[Math.round(x*expected.width-.5),Math.round(y*expected.height-.5)]),expected.points);
});

test('the 30 LED jacket simulator receives the 2D field in physical output order', async () => {
    const load = file => import('data:text/javascript;base64,' + Buffer.from(readFileSync(new URL(file, import.meta.url), 'utf8')).toString('base64'));
    const { NEBULITE_SETUP_PROFILES } = await load('../../src/ui/nebulite-profile-data.js');
    const { productProfileMatches, productProjection, productProjectionPoints, productProjectionProfile } = await load('../../src/ui/nebulite-setup.js');
    const base=NEBULITE_SETUP_PROFILES.find(item=>item.name==='NEBULITE Jacket S3');
    const profile=productProjectionProfile(base,'virtual2d');
    const system={name:'System',controls:[{name:'deviceModel',value:base.name}]};
    const modules=[system,...['Drivers','Layouts'].map(name=>({name,children:Object.entries(profile.state[name]).filter(([,v])=>v?.type).map(([name,v])=>({name,type:v.type,role:'driver',controls:Object.entries(v).filter(([key])=>key!=='type').map(([name,value])=>({name,value}))}))}))];
    modules[2].children[0].controls.find(control=>control.name==='points').value = profile.state.Layouts.ProductStrip.points.replace('5,14;', '6,14;');
    const sampled=[];let rendered=0;
    const context=contextFor(['drawHelioCardTracePreview'],{state:{modules},NEBULITE_SETUP_PROFILES,
        isNebuliteFirmware:()=>true,findModule:()=>system,controlValue:module=>module.controls[0].value,
        productProfileMatches,productProjection,productProjectionPoints,productProjectionProfile,
        requestNebulitePreviewMetadata(){},setHelioPreviewKind(){},
        drawJacketPreview:(_canvas,t,sampler,projection)=>{rendered++;assert.deepEqual(projection.points[0],[5,14]);for(let i=0;i<30;i++)sampler((i+.5)/30,.5,t);return projection.points.length===30;}});
    context.drawHelioCardTracePreview({},0,(x,y)=>{sampled.push([x,y]);return [0,0,0];},30,{effect:{type:'PlasmaEffect'}});
    const p=productProjectionPoints(base,modules);
    assert.equal(rendered,1);
    assert.deepEqual(sampled.map(([x,y])=>[Math.round(x*p.width-.5),Math.round(y*p.height-.5)]),p.points);
});

test('crop-top preview loads missing driver metadata once and repaints after readback', async () => {
    const pending = deferred(); let reads = 0, repaints = 0;
    const context = contextFor(['requestNebulitePreviewMetadata'], {
        nebulitePreviewMetadataPending: false, nebulitePreviewMetadataRetryAt: 0,
        performance: { now: () => 100 }, helioNeedsModuleMetadata: name => name === 'Drivers',
        ensureHelioModuleMetadata: () => { reads++; return pending.promise; },
        refreshHistoricalCroptopPreviews: () => { repaints++; },
    });
    context.requestNebulitePreviewMetadata(); context.requestNebulitePreviewMetadata();
    assert.equal(reads, 1); assert.equal(repaints, 0);
    pending.resolve(true); await until(() => repaints === 1);
    await until(() => context.nebulitePreviewMetadataPending === false);
});

test('live garment wiring changes invalidate previews only when their renderer can change', () => {
    let matching = true, repaints = 0, liveRepaints = 0, count = 9;
    const model = { name: 'NEBULITE Croptop S3' };
    const context = contextFor(['syncNebulitePreviewProfile'], {
        state: { modules: [] }, nebulitePreviewProfileSignature: null,
        NEBULITE_SETUP_PROFILES: [model], findModule: () => ({}),
        controlValue: () => model.name, helioCurrentCount: () => count,
        productProfileMatches: () => matching, refreshHistoricalCroptopPreviews: () => { repaints++; },
        drawNebuliteLiveStage: () => { liveRepaints++; },
    });
    context.syncNebulitePreviewProfile();
    context.syncNebulitePreviewProfile();
    assert.equal(repaints, 0);
    matching = false;
    context.syncNebulitePreviewProfile();
    context.syncNebulitePreviewProfile();
    assert.equal(repaints, 1, 'a custom wiring stops using the old garment rendering');
    matching = true; count = 8;
    context.syncNebulitePreviewProfile();
    assert.equal(repaints, 2, 'a changed logical length also repaints the preview');
    assert.equal(liveRepaints, 2);
});

test('the live garment stage hides a stale or unverified source rather than showing an old frame', () => {
    const canvas = { hidden: true }, image = { hidden: false }, label = { textContent: '' };
    let fresh = true, matches = true, drawn = 0;
    const model = { name: 'NEBULITE Croptop S3' };
    const context = contextFor(['drawNebuliteLiveStage'], {
        document: { getElementById: () => canvas, querySelector: selector =>
            selector === '.nebulite-product-image' ? image : label },
        state: { modules: [] }, NEBULITE_SETUP_PROFILES: [model],
        findModule: () => ({}), controlValue: () => model.name,
        productProfileMatches: () => matches, helioLivePreviewFresh: () => fresh,
        nebuliteLiveFrame: {}, nebuliteLiveSampler: () => () => [0, 0, 0],
        drawHistoricalCroptop: () => { drawn++; return true; }, whenHistoricalCroptopReady() {},
    });
    context.drawNebuliteLiveStage();
    assert.equal(canvas.hidden, false); assert.equal(image.hidden, true);
    assert.equal(label.textContent, 'Live on the crop-top illustration');
    fresh = false; context.drawNebuliteLiveStage();
    assert.equal(canvas.hidden, true); assert.equal(image.hidden, false);
    fresh = true; matches = false; context.drawNebuliteLiveStage();
    assert.equal(canvas.hidden, true); assert.equal(drawn, 1);
});

test('a set-up product without an illustration shows its live strip at the layout\'s light count', () => {
    const canvas = { hidden: true }, image = { hidden: false }, label = { textContent: '' };
    const strips = [];
    const model = { name: 'NEBULITE Jacket ESP32' };
    const context = contextFor(['drawNebuliteLiveStage'], {
        document: { getElementById: () => canvas, querySelector: selector =>
            selector === '.nebulite-product-image' ? image : label },
        state: { modules: [] }, NEBULITE_SETUP_PROFILES: [model],
        findModule: () => ({}), controlValue: () => model.name, helioCurrentCount: () => 50,
        productProfileMatches: () => true, helioLivePreviewFresh: () => true,
        nebuliteLiveFrame: {}, nebuliteLiveSampler: (_frame, count) => { strips.push(['sampler', count]); return () => [0, 0, 0]; },
        drawNebuliteStripPreview: (_canvas, _t, _sampler, count) => { strips.push(['strip', count]); return true; },
    });
    context.drawNebuliteLiveStage();
    assert.deepEqual(strips, [['sampler', 50], ['strip', 50]]);
    assert.equal(canvas.hidden, false); assert.equal(image.hidden, true);
    assert.equal(label.textContent, 'Live on a 50 LED strip');
});

test('garment preview refresh clears old historical and fallback renderers', () => {
    let selector = '', restarts = 0; const cleared = [];
    const context = contextFor(['refreshHistoricalCroptopPreviews'], {
        document: { querySelectorAll: value => { selector = value; return ['old-canvas']; } },
        clearHelioPreviewRenderState: canvas => cleared.push(canvas),
        startHelioModePreviews: () => { restarts++; },
    });
    context.refreshHistoricalCroptopPreviews();
    assert.match(selector, /historical-croptop-simulation/);
    assert.match(selector, /nebulite-strip-simulation/);
    assert.match(selector, /jacket-30-simulation/);
    assert.deepEqual(cleared, ['old-canvas']);
    assert.equal(restarts, 1);
});

test('scene operations share their actual nested state-document paths', () => {
    const { modules } = tree();
    const before = plain(modules);
    assert.deepEqual(planHelioControlDocument(modules, [
        { module: 'Old', control: 'enabled', value: false },
        { module: 'Twinkles', control: 'speed', value: 90 },
        { module: 'Twinkles', control: 'enabled', value: true },
        { module: 'Effects', control: 'enabled', value: true },
    ]), { Effects: { enabled: true, Layer: { Old: { enabled: false }, Twinkles: { speed: 90, enabled: true } } } });
    assert.deepEqual(modules, before, 'planning preserves the confirmed state');
    assert.throws(() => planHelioControlDocument(modules, [{ module: 'Removed', control: 'enabled', value: true }]),
        /Unknown module/);
});

function senderFixture(apiFetch) {
    const fixture = tree();
    const context = contextFor(['sendHelioControlOperations', 'updateLocalControlValue', 'activateHelioEffect'], {
        state: { modules: fixture.modules }, helioPresetModules: new Map(), ws: null, WebSocket: { OPEN: 1 }, apiFetch,
        errorMessage: async response => `HTTP ${response.status}`,
        planHelioControlDocument, planHelioEffectActivation,
        findModule: name => [fixture.old, fixture.target].find(module => module.name === name),
        helioEffects: () => [fixture.old, fixture.target],
        controlValuesEqual: (control, value) => control?.value === value,
        helioControlValueForSend: (_control, value) => value,
    });
    return { ...fixture, context };
}

test('a scene uses one PATCH and changes local controls only after both HTTP and JSON success', async () => {
    const response = deferred();
    const result = deferred();
    const requests = [];
    const fixture = senderFixture((url, options) => { requests.push({ url, options }); return response.promise; });
    const pending = fixture.context.activateHelioEffect(fixture.target, { speed: 90 });
    assert.equal(requests.length, 1);
    assert.equal(requests[0].url, '/api/state');
    assert.equal(requests[0].options.method, 'PATCH');
    assert.deepEqual(JSON.parse(requests[0].options.body), {
        Effects: { Layer: { Old: { enabled: false }, Twinkles: { speed: 90, enabled: true } } },
    });
    const unchanged = () => {
        assert.equal(fixture.old.enabled, true);
        assert.equal(fixture.target.enabled, false);
        assert.equal(fixture.target.controls[0].value, 20);
    };
    unchanged();
    let readingBody = false;
    response.resolve({ ok: true, json: () => { readingBody = true; return result.promise; } });
    await until(() => readingBody);
    unchanged();
    result.resolve({ ok: true });
    assert.equal(await pending, true);
    assert.equal(fixture.old.enabled, false);
    assert.equal(fixture.target.enabled, true);
    assert.equal(fixture.target.controls[0].value, 90);
});

for (const failure of ['http', 'refused', 'network', 'invalid-json']) {
    test(`${failure} scene failure preserves the confirmed local state`, async () => {
        let requests = 0;
        const fixture = senderFixture(async () => {
            requests++;
            if (failure === 'network') throw new Error('offline');
            return { ok: failure !== 'http', status: 400, json: async () => {
                if (failure === 'invalid-json') throw new SyntaxError('truncated');
                return { ok: false, error: 'validation refused' };
            } };
        });
        const before = plain(fixture.modules);
        assert.equal(await fixture.context.activateHelioEffect(fixture.target, { speed: 90 }), false);
        assert.equal(requests, 1);
        assert.deepEqual(fixture.modules, before);
    });
}

test('an empty scene operation list needs no request', async () => {
    const fixture = senderFixture(() => assert.fail('an unchanged scene sends no PATCH'));
    assert.equal(await fixture.context.sendHelioControlOperations([]), true);
});

function panelFixture() {
    const panel = { children: [{ tagName: 'INPUT', disabled: false }], attributes: {},
        setAttribute(name, value) { this.attributes[name] = value; },
        replaceChildren(...children) { this.children = children; } };
    const selection = [];
    return { panel, selection, document: {
        querySelector: selector => selector === '.helio-controls' ? panel : null,
        createElement: tagName => ({ tagName, attributes: {}, setAttribute(name, value) { this.attributes[name] = value; } }),
    } };
}

test('pending selection immediately removes old editable scene controls and exposes busy status', () => {
    const fixture = panelFixture();
    const context = contextFor(['showHelioScenePending'], {
        document: fixture.document, setHelioModeCardActive: id => fixture.selection.push(id),
    });
    const status = {};
    context.showHelioScenePending(scene('C'), status);
    assert.deepEqual(fixture.selection, ['C']);
    assert.equal(fixture.panel.attributes['aria-busy'], 'true');
    assert.equal(fixture.panel.children[0].textContent, 'Scene C');
    assert.equal(fixture.panel.children[1].attributes.role, 'status');
    assert.ok(fixture.panel.children.every(child => !['INPUT', 'SELECT', 'TEXTAREA'].includes(child.tagName.toUpperCase())));
});

function interactionFixture(overrides = {}) {
    const fixture = panelFixture();
    const effects = ['Old', 'A', 'B', 'C'].map(id => ({ name: id, type: `${id}Effect`, enabled: id === 'Old' }));
    const gates = { A: deferred(), C: deferred() };
    const sent = [];
    const restored = [];
    const current = () => effects.find(effect => effect.enabled);
    const restore = () => {
        restored.push(current().name);
        fixture.panel.attributes['aria-busy'] = 'false';
        fixture.panel.children = [{ tagName: 'INPUT', effect: current().name }];
    };
    const context = contextFor(['showHelioScenePending', 'applyHelioMode'], {
        state: { modules: [{ name: 'Effects' }] }, document: fixture.document,
        helioModeApplying: false, helioModeApplyingId: '', helioModeQueued: null, helioModeGeneration: 0,
        helioSceneResyncExpected: false, ws: null, WebSocket: { OPEN: 1 },
        setHelioModeCardActive: id => fixture.selection.push(id), stopHelioPlaylist() {},
        pauseHelioCardPreviews() {}, resumeHelioCardPreviews() {}, clearHelioLivePreview() {},
        helioSceneUsesAudio: () => false, firstModuleByType: () => ({ name: 'Layer' }),
        ensureHelioEffectsState: async () => true,
        helioEffects: () => effects, helioActiveEffect: current, applyHelioScenePalette: async () => {},
        activateHelioEffect: async target => {
            sent.push(target.name);
            const ok = await gates[target.name].promise;
            if (ok) effects.forEach(effect => { effect.enabled = effect === target; });
            return ok;
        }, syncHelioDashboardControls: restore, refreshHelioControlPanel: restore,
        ...overrides,
    });
    return { ...fixture, context, effects, gates, sent, restored };
}

test('rapid A then B then C selects C immediately and applies only the latest queued intent', async () => {
    const fixture = interactionFixture();
    const pending = fixture.context.applyHelioMode(scene('A'), {});
    await until(() => fixture.sent.length === 1);
    assert.equal(await fixture.context.applyHelioMode(scene('B'), {}), false);
    assert.equal(await fixture.context.applyHelioMode(scene('C'), {}), false);
    assert.equal(fixture.selection.at(-1), 'C');
    assert.equal(fixture.panel.children[0].textContent, 'Scene C');
    fixture.gates.A.resolve(true);
    await until(() => fixture.sent.length === 2);
    assert.deepEqual(fixture.sent, ['A', 'C']);
    assert.deepEqual(fixture.restored, [], 'A cannot replace C pending controls with stale confirmed controls');
    assert.equal(fixture.panel.children[0].textContent, 'Scene C');
    fixture.gates.C.resolve(true);
    await pending;
    assert.equal(fixture.effects.find(effect => effect.enabled).name, 'C');
    assert.ok(fixture.restored.every(name => name === 'C'));
});

test('failed scene activation restores controls for the last confirmed scene', async () => {
    const fixture = interactionFixture();
    const status = {};
    const pending = fixture.context.applyHelioMode(scene('A'), status);
    await until(() => fixture.sent.length === 1);
    fixture.gates.A.resolve(false);
    assert.equal(await pending, false);
    assert.equal(status.textContent, 'apply failed');
    assert.deepEqual(fixture.restored, ['Old']);
    assert.equal(fixture.panel.children[0].effect, 'Old');
});

test('a scene absent from the tree hides old controls before replace responds and restores them on refusal', async () => {
    const replace = deferred();
    let requests = 0;
    const fixture = interactionFixture({
        helioSceneReplacementSlot: () => ({ name: 'Old' }),
        replaceModule: () => { requests++; return replace.promise; }, addModule: async () => false,
    });
    const pending = fixture.context.applyHelioMode(scene('Missing'), {});
    await until(() => requests === 1);
    assert.equal(fixture.panel.attributes['aria-busy'], 'true');
    assert.equal(fixture.panel.children[0].textContent, 'Scene Missing');
    assert.deepEqual(fixture.restored, []);
    replace.resolve(false);
    assert.equal(await pending, false);
    assert.deepEqual(fixture.restored, ['Old']);
});

for (const type of ['display', 'display-int', 'time']) {
    test(`all copies of a read-only ${type} receive fresh values without rebuilding their container`, () => {
        const spans = [{ dataset: { unit: 'old' }, textContent: 'stale' }, { dataset: {}, textContent: 'stale' }];
        let selections = 0;
        const context = contextFor(['updateModuleControls'], {
            document: { querySelectorAll: selector => {
                assert.equal(selector, 'span.display[data-mid="Ambient"][data-key="target"]');
                selections++;
                return spans;
            } },
            syncVisibleControls: () => false, cssEscape: value => value, dragTs: {},
            EDITABLE_CONTROL_TYPES: new Set(), fmtDisplayInt: control => `${control.value} ${control.unit}`,
            fmtTime: value => `${value}s`, defaultFor: () => undefined,
        });
        const control = { name: 'target', type, value: 120, unit: '%' };
        context.updateModuleControls({ name: 'Ambient', controls: [control] });
        const expected = type === 'display-int' ? '120 %' : type === 'time' ? '120s' : 120;
        assert.deepEqual(spans.map(span => span.textContent), [expected, expected]);
        control.value = 140;
        context.updateModuleControls({ name: 'Ambient', controls: [control] });
        assert.equal(selections, 2);
        assert.equal(spans[0].textContent, spans[1].textContent);
        if (type === 'display-int') assert.deepEqual(spans.map(span => span.dataset.unit), ['%', '%']);
    });
}

function previewFixture() {
    const canvas = (name, kind, options = {}) => ({
        name, kind, dataset: options.missing ? {} : { previewRenderer: 'painted', previewMax: '255' },
        getBoundingClientRect: () => ({ width: 180, height: 180,
            top: options.offscreen ? 1000 : 20, bottom: options.offscreen ? 1180 : 200 }),
        closest: () => ({ classList: { contains: value => value === 'active' && !!options.active },
            matches: () => !!options.hover || !!options.focus }),
    });
    const canvases = [canvas('static', 'helio'), canvas('active', 'helio', { active: true }),
        canvas('hover', 'helio', { hover: true }), canvas('focus', 'helio', { focus: true }),
        canvas('new', 'helio', { missing: true }), canvas('offscreen', 'helio', { offscreen: true }),
        canvas('preset-static', 'preset'), canvas('preset-hover', 'preset', { hover: true }),
        canvas('preset-new', 'preset', { missing: true })];
    const clocks = new WeakMap(canvases.map(item => [item, { sceneTime: 1 }]));
    const context = contextFor(['helioPreviewItems', 'helioPreviewNearViewport', 'clearHelioPreviewRenderState'], {
        document: { querySelectorAll: selector => canvases.filter(item =>
            item.kind === (selector === '.helio-mode-preview' ? 'helio' : 'preset')) },
        window: { innerHeight: 800 }, HELIO_PREVIEW_VIEWPORT_MARGIN_PX: 0, helioPreviewClocks: clocks,
    });
    return { context, canvases, clocks };
}

test('steady preview animation includes active, hover and focus cards plus missing snapshots, never static or offscreen cards', () => {
    const { context, canvases, clocks } = previewFixture();
    const names = context.helioPreviewItems(false, { animateOnly: true, includeMissing: true })
        .map(item => item.canvas.name);
    assert.deepEqual(plain(names), ['active', 'hover', 'focus', 'new', 'preset-hover', 'preset-new']);
    const offscreen = canvases.find(item => item.name === 'offscreen');
    assert.equal(offscreen.dataset.previewRenderer, undefined);
    assert.equal(offscreen.dataset.previewMax, undefined);
    assert.equal(clocks.has(offscreen), false);
});

test('missing preview snapshots render once while painted idle cards retain their snapshots', () => {
    const { context } = previewFixture();
    const missing = context.helioPreviewItems(false, { missingOnly: true });
    assert.deepEqual(plain(missing.map(item => item.canvas.name)), ['new', 'preset-new']);
    for (const item of missing) item.canvas.dataset.previewRenderer = 'painted';
    assert.equal(context.helioPreviewItems(false, { missingOnly: true }).length, 0);
    assert.deepEqual(plain(context.helioPreviewItems(false, { animateOnly: true, includeMissing: true })
        .map(item => item.canvas.name)), ['active', 'hover', 'focus', 'preset-hover']);
});

test('preview frame budget yields after an expensive card and resumes at the next card without skips', () => {
    let now = 0;
    const painted = [];
    const context = contextFor(['paintHelioPreviewBatch'], {
        performance: { now: () => now }, helioPreviewBatch: ['A', 'B', 'C'], helioPreviewBatchIndex: 0,
        paintHelioPreviewItem: item => { painted.push(item); now += 9; },
    });
    context.paintHelioPreviewBatch(0, 3);
    assert.deepEqual(painted, ['A']);
    assert.equal(context.helioPreviewBatchIndex, 1);
    context.paintHelioPreviewBatch(16, 3);
    assert.deepEqual(painted, ['A', 'B']);
    assert.equal(context.helioPreviewBatchIndex, 2);
    context.paintHelioPreviewBatch(32, 3);
    assert.deepEqual(painted, ['A', 'B', 'C']);
    assert.equal(context.helioPreviewBatchIndex, 3);
});

test('background preset preview hydration cools down after failure and clears its deadline on success', async () => {
    let now = 100, restarted = 0;
    const record = { name: 'Orange & blue', editor: null, loading: null };
    const gates = [deferred(), deferred()];
    const calls = [], draws = [];
    const canvas = { dataset: { nativePreset: record.name } };
    const collection = { records: new Map([[record.name, record]]), ensureEditor(target, options) {
        calls.push({ target, options });
        const gate = gates[calls.length - 1];
        record.loading = gate.promise.then(editor => { record.editor = editor; return editor; })
            .finally(() => { record.loading = null; });
        return record.loading;
    } };
    const context = contextFor(['paintHelioScenePreview'], {
        performance: { now: () => now }, helioPresetCollection: collection,
        startHelioModePreviews: () => { ++restarted; }, helioPresetModule: () => ({ type: 'SolidEffect' }),
        helioControl: () => ({}), helioChoiceIndex: () => 0, paletteByIndex: () => 'palette',
        makeHelioSampler: () => 'sampler', helioCurrentCount: () => 222,
        drawHelioCardTracePreview: (...args) => draws.push(args),
    });
    context.paintHelioScenePreview(canvas, 1);
    context.paintHelioScenePreview(canvas, 2);
    assert.equal(calls.length, 1, 'an in-flight background load is not duplicated');
    assert.equal(calls[0].target, record);
    assert.equal(calls[0].options.background, true);
    now = 350;
    gates[0].reject(new Error('503 temporary pressure'));
    await until(() => record.previewRetryAt === 2350);
    assert.equal(record.loading, null);
    for (const instant of [350, 1000, 2349]) {
        now = instant;
        context.paintHelioScenePreview(canvas, 3);
    }
    assert.equal(calls.length, 1, 'animation frames during cooldown cannot flood GET requests');
    now = 2350;
    context.paintHelioScenePreview(canvas, 4);
    assert.equal(calls.length, 2);
    gates[1].resolve({ document: { Effects: {} } });
    await until(() => record.previewRetryAt === 0 && restarted === 1);
    context.paintHelioScenePreview(canvas, 5);
    assert.equal(calls.length, 2);
    assert.equal(draws.length, 1);
    assert.equal(draws[0][0], canvas);
    assert.equal(draws[0][1], 5);
});

const moduleResponse = module => ({ ok: true, json: async () => module });
function effectsReadbackFixture(overrides = {}) {
    const old = { name: 'Effects', controls: [], children: [{ name: 'Layer', controls: [],
        children: [{ name: 'Old', type: 'OldEffect', enabled: true, controls: [{ name: 'speed', value: 9 }] }] }] };
    const drivers = { name: 'Drivers', controls: [{ name: 'on', value: true }], children: [] };
    const requests = [];
    const gates = [];
    let renders = 0;
    let context;
    context = contextFor(['readModuleData', 'invalidateHelioEffects', 'ensureHelioEffectsState',
        'replaceModuleInState', 'findModule', 'helioActiveEffect'], {
        state: { modules: [old, drivers] }, selectedModule: 'Old',
        helioEffectsGeneration: 0, helioEffectsStale: false, helioEffectsReadback: null,
        helioModeGeneration: 0, helioModeQueued: null,
        helioPresetCollection: { applying: false, pending: null },
        renderCards: () => { ++renders; }, waitMs: async () => {},
        apiFetch(url) { const gate = deferred(); requests.push(url); gates.push(gate); return gate.promise; },
        errorMessage: async response => `HTTP ${response.status}`,
        helioEffects: () => context.state.modules.find(module => module.name === 'Effects').children[0].children,
        helioPreferredActiveEffect: effects => effects.find(effect => effect.enabled),
        ...overrides,
    });
    return { context, old, drivers, requests, gates, renders: () => renders };
}
const freshEffects = name => ({ name: 'Effects', controls: [{ name: 'enabled', value: true }],
    children: [{ name: 'Fresh Layer', controls: [], children: [{ name, type: 'FreshEffect', enabled: true, controls: [] }] }] });

test('targeted module reads encode the actual name and reject HTTP or malformed module responses', async () => {
    const name = 'Studio Audio / input';
    for (const response of [
        { ok: false, status: 503 },
        moduleResponse({ name: 'Other', controls: [] }),
        moduleResponse({ name, controls: {} }),
        { ok: true, json: async () => { throw new SyntaxError('truncated'); } },
    ]) {
        const requests = [];
        const context = contextFor(['readModuleData'], {
            apiFetch: async url => { requests.push(url); return response; }, errorMessage: async () => 'HTTP 503',
        });
        await assert.rejects(context.readModuleData(name));
        assert.deepEqual(requests, ['/api/modules/Studio%20Audio%20%2F%20input']);
    }
});

test('effects readback waits for both apply and pending intent then replaces only the complete Effects resource', async () => {
    const waits = [];
    const fixture = effectsReadbackFixture({ waitMs(ms) {
        const gate = deferred(); waits.push({ ms, ...gate }); return gate.promise;
    } });
    fixture.context.invalidateHelioEffects();
    assert.equal(fixture.context.selectedModule, 'Effects');
    assert.equal(fixture.renders(), 1);
    assert.equal(fixture.context.helioActiveEffect(), null);
    fixture.context.helioPresetCollection.applying = true;
    const first = fixture.context.ensureHelioEffectsState();
    const shared = fixture.context.ensureHelioEffectsState();
    assert.equal(waits.length, 1);
    assert.equal(waits[0].ms, 25);
    assert.deepEqual(fixture.requests, []);
    fixture.context.helioPresetCollection.applying = false;
    fixture.context.helioPresetCollection.pending = { record: 'Latest' };
    waits[0].resolve();
    await until(() => waits.length === 2);
    assert.deepEqual(fixture.requests, []);
    fixture.context.helioPresetCollection.pending = null;
    waits[1].resolve();
    await until(() => fixture.requests.length === 1);
    const fresh = freshEffects('Latest');
    fixture.gates[0].resolve(moduleResponse(fresh));
    assert.equal(await first, true);
    assert.equal(await shared, true);
    assert.deepEqual(fixture.requests, ['/api/modules/Effects']);
    assert.equal(fixture.context.state.modules[0], fresh);
    assert.equal(fixture.context.state.modules[1], fixture.drivers);
    assert.equal(fixture.context.findModule('Old'), null);
    assert.equal(fixture.context.helioActiveEffect().name, 'Latest');
    assert.equal(fixture.context.helioEffectsStale, false);
    assert.equal(fixture.context.helioEffectsReadback, null);
});

test('an invalidated in-flight Effects response never commits and retries the latest generation', async () => {
    const fixture = effectsReadbackFixture();
    fixture.context.invalidateHelioEffects();
    const reading = fixture.context.ensureHelioEffectsState();
    assert.deepEqual(fixture.requests, ['/api/modules/Effects']);
    fixture.context.invalidateHelioEffects();
    fixture.gates[0].resolve(moduleResponse(freshEffects('Obsolete')));
    await until(() => fixture.requests.length === 2);
    assert.equal(fixture.context.state.modules[0], fixture.old);
    assert.equal(fixture.context.helioActiveEffect(), null);
    const newest = freshEffects('Newest');
    fixture.gates[1].resolve(moduleResponse(newest));
    assert.equal(await reading, true);
    assert.equal(fixture.context.state.modules[0], newest);
    assert.deepEqual(fixture.requests, ['/api/modules/Effects', '/api/modules/Effects']);
    assert.equal(fixture.context.helioActiveEffect().name, 'Newest');
});

test('an obsolete HTTP or JSON read failure retries the newer Effects generation rather than ending readback', async () => {
    for (const obsolete of [{ ok: false, status: 503 },
        { ok: true, json: async () => { throw new SyntaxError('obsolete truncated body'); } }]) {
        const fixture = effectsReadbackFixture();
        fixture.context.invalidateHelioEffects();
        const reading = fixture.context.ensureHelioEffectsState();
        fixture.context.invalidateHelioEffects();
        fixture.gates[0].resolve(obsolete);
        await until(() => fixture.requests.length === 2);
        assert.equal(fixture.context.state.modules[0], fixture.old);
        assert.equal(fixture.context.helioEffectsStale, true);
        assert.equal(fixture.context.helioActiveEffect(), null);
        fixture.gates[1].resolve(moduleResponse(freshEffects('Recovered Latest')));
        assert.equal(await reading, true);
        assert.equal(fixture.context.helioActiveEffect().name, 'Recovered Latest');
        assert.deepEqual(fixture.requests, ['/api/modules/Effects', '/api/modules/Effects']);
    }
});

test('a failed targeted read retains stale status and permits a deliberate retry without exposing old controls', async () => {
    const fixture = effectsReadbackFixture();
    fixture.context.invalidateHelioEffects();
    const failed = fixture.context.ensureHelioEffectsState();
    fixture.gates[0].resolve({ ok: false, status: 503 });
    assert.equal(await failed, false);
    assert.equal(fixture.context.state.modules[0], fixture.old);
    assert.equal(fixture.context.helioEffectsStale, true);
    assert.equal(fixture.context.helioEffectsReadback, null);
    assert.equal(fixture.context.helioActiveEffect(), null);
    const retry = fixture.context.ensureHelioEffectsState();
    fixture.gates[1].resolve(moduleResponse(freshEffects('Recovered')));
    assert.equal(await retry, true);
    assert.equal(fixture.context.helioActiveEffect().name, 'Recovered');
});

test('a late full WebSocket snapshot cannot mark stale Effects fresh before the targeted read', async () => {
    const fixture = effectsReadbackFixture();
    let snapshots = 0;
    Object.assign(fixture.context, { ws: null, wsReconnectTimer: null, wsPaused: false,
        location: { host: 'helio.test' },
        WebSocket: class { constructor(url) { this.url = url; } },
        syncHelioBatteryStatus() {},
        renderStateSnapshot() { ++snapshots; },
    });
    vm.runInContext(functionSource('connectWs'), fixture.context);
    fixture.context.invalidateHelioEffects();
    fixture.context.connectWs();
    fixture.context.ws.onmessage({ data: JSON.stringify({ modules: [freshEffects('Old WS'), fixture.drivers] }) });
    assert.equal(snapshots, 1);
    assert.equal(fixture.context.helioEffectsStale, true);
    assert.equal(fixture.context.helioActiveEffect(), null);
    assert.deepEqual(fixture.requests, []);
    const reading = fixture.context.ensureHelioEffectsState();
    fixture.gates[0].resolve(moduleResponse(freshEffects('HTTP Confirmed')));
    assert.equal(await reading, true);
    assert.equal(fixture.context.helioActiveEffect().name, 'HTTP Confirmed');
    assert.deepEqual(fixture.requests, ['/api/modules/Effects']);
});

test('legacy activation waits for fresh live Effects before sending its scene operation', async () => {
    const fresh = deferred();
    const fixture = interactionFixture({ ensureHelioEffectsState: () => fresh.promise });
    const playing = fixture.context.applyHelioMode(scene('A'), {});
    assert.deepEqual(fixture.sent, []);
    fresh.resolve(true);
    await until(() => fixture.sent.length === 1);
    fixture.gates.A.resolve(true);
    assert.equal(await playing, true);
});

function nativeSelectionFor(fixture) {
    let callbacks;
    Object.assign(fixture.context, {
        helioPresetCollection: null, selectedModule: '@helio', helioEffectsGeneration: 0, helioEffectsStale: false,
        createPresetClient: () => ({}), apiFetch() { assert.fail('selection alone must not send a controller write'); },
        HelioPresetCards: class { constructor(options) { callbacks = options; } mount() {} },
    });
    vm.runInContext(['findModule', 'invalidateHelioEffects', 'mountHelioPresetCards'].map(functionSource).join('\n'),
        fixture.context);
    fixture.context.mountHelioPresetCards({});
    return record => callbacks.onSelect(record);
}

test('a native selection cancels a waiting legacy activation and its older queued scene before either can write', async () => {
    const fresh = deferred();
    const writes = [];
    let reads = 0;
    const fixture = interactionFixture({
        ensureHelioEffectsState: () => { ++reads; return fresh.promise; },
        applyHelioScenePalette: async () => { writes.push('palette'); },
        activateHelioEffect: async target => { writes.push(target.name); return true; },
    });
    const selectNative = nativeSelectionFor(fixture);
    const playing = fixture.context.applyHelioMode(scene('A'), {});
    await until(() => reads === 1);
    assert.equal(await fixture.context.applyHelioMode(scene('B'), {}), false);
    assert.equal(fixture.context.helioModeQueued.mode.id, 'B');

    selectNative({ name: 'Native C' });

    assert.equal(fixture.context.helioModeGeneration, 1);
    assert.equal(fixture.context.helioModeQueued, null);
    assert.equal(fixture.context.helioEffectsStale, true);
    fresh.resolve(true);
    assert.equal(await playing, false);
    assert.deepEqual(writes, []);
    assert.deepEqual(fixture.restored, [], 'the cancelled activation cannot restore old editable controls');
    assert.equal(fixture.context.helioModeApplying, false);
});

test('native supersession drops old queued intent while a genuinely newer legacy click can still run', async () => {
    for (const newer of ['A', 'C']) {
        const fresh = deferred();
        const writes = [];
        let reads = 0;
        let fixture;
        fixture = interactionFixture({
            ensureHelioEffectsState: () => ++reads === 1 ? fresh.promise : Promise.resolve(true),
            applyHelioScenePalette: async () => { writes.push('palette'); },
            activateHelioEffect: async target => {
                writes.push(target.name);
                fixture.effects.forEach(effect => { effect.enabled = effect === target; });
                return true;
            },
        });
        const selectNative = nativeSelectionFor(fixture);
        const playing = fixture.context.applyHelioMode(scene('A'), {});
        await until(() => reads === 1);
        await fixture.context.applyHelioMode(scene('B'), {});
        selectNative({ name: 'Native choice' });
        await fixture.context.applyHelioMode(scene(newer), {});
        assert.equal(fixture.context.helioModeQueued.mode.id, newer);
        fresh.resolve(true);
        assert.equal(await playing, false);
        assert.deepEqual(writes, ['palette', newer]);
        assert.equal(fixture.effects.find(effect => effect.enabled).name, newer);
        assert.ok(fixture.restored.every(name => name === newer));
    }
});

test('audio setup refreshes the actual Audio module without downloading the full module tree', async () => {
    const audio = { name: 'Studio Audio', controls: [{ name: 'enabled', value: false }] };
    const fresh = { name: audio.name, controls: [{ name: 'enabled', value: true }] };
    const calls = [], operations = [];
    let context;
    context = contextFor(['ensureHelioAudioReady', 'readModuleData', 'replaceModuleInState'], {
        state: { modules: [audio] }, HELIO_DEFAULT_AUDIO: { enabled: true, gain: 10 },
        helioAudio: () => context.state.modules[0],
        helioAudioStartEnabled: module => module.controls[0].value,
        sendHelioControlOperations: async ops => { operations.push(plain(ops)); return true; },
        waitMs: async () => {}, updateHelioHome() {},
        apiFetch: async url => { calls.push(url); return moduleResponse(fresh); },
        errorMessage: async () => 'unavailable',
    });
    const status = {};
    assert.equal(await context.ensureHelioAudioReady(status), true);
    assert.deepEqual(calls, ['/api/modules/Studio%20Audio']);
    assert.deepEqual(operations, [[{ module: 'Studio Audio', control: 'enabled', value: true },
        { module: 'Studio Audio', control: 'gain', value: 10 }]]);
    assert.equal(context.state.modules[0], fresh);
    assert.equal(status.textContent, 'audio ready');
});

test('master power and palette controls send only their control write without a full state refresh', async () => {
    const drivers = { name: 'Drivers', type: 'Drivers', role: 'generic',
        controls: [{ name: 'on', value: true }, { name: 'palette', value: 1 }] };
    const calls = [];
    const context = contextFor(['sendControl', 'updateLocalControlValue', 'findModule'], {
        state: { modules: [drivers] }, helioPresetModules: new Map(),
        allModules: () => [drivers], helioPlaylistRun: null, helioPlaylistApplying: false,
        helioControlAffectsPreview: () => true, refreshHelioTrace() {},
        apiFetch: async (url, options) => { calls.push({ url, options }); return { ok: true }; },
        refetchState() { assert.fail('a master control must not trigger full state hydration'); },
        refreshStateData() { assert.fail('a master control must not trigger full state hydration'); },
    });
    assert.equal(await context.sendControl('Drivers', 'on', false), true);
    assert.equal(await context.sendControl('Drivers', 'palette', 7), true);
    assert.deepEqual(calls.map(call => call.url), ['/api/control', '/api/control']);
    assert.deepEqual(calls.map(call => JSON.parse(call.options.body)), [
        { module: 'Drivers', control: 'on', value: false }, { module: 'Drivers', control: 'palette', value: 7 },
    ]);
    assert.equal(drivers.controls[0].value, false);
    assert.equal(drivers.controls[1].value, 7);
});

test('the dashboard distinguishes pending playback readback from a confirmed Off scene', () => {
    const sceneLabel = { textContent: '', closest: () => null };
    const context = contextFor(['updateHelioHome', 'helioActiveEffect'], {
        document: { querySelector: () => ({}), querySelectorAll: selector =>
            selector === '[data-helio-field="scene"]' ? [sceneLabel] : [] },
        HTMLInputElement: class {}, HTMLSelectElement: class {},
        helioEffectsStale: true, helioModeApplying: true,
        syncHelioBatteryStatus() {}, helioAmbientLight: () => null,
        helioModeForEffect: () => null, helioDrivers: () => ({}),
        helioAmbientBrightnessOverride: () => ({ active: false }),
        helioCurrentCount: () => 222, helioBrightnessLabel: () => '', helioConnectionLabel: () => '',
        controlValue: (_module, _name, fallback) => fallback,
        updateHelioBrightnessOverrideNote() {}, helioChoiceIndex: () => 0, helioControl: () => null,
        helioRmtDriver: () => null, helioColorHex: () => '', helioEffects: () => [],
        helioPreferredActiveEffect: () => null,
    });
    context.updateHelioHome();
    assert.equal(sceneLabel.textContent, 'Playback state pending');
    context.helioEffectsStale = false;
    context.updateHelioHome();
    assert.equal(sceneLabel.textContent, 'Off');
});

test('the generic Effects view shows a read-only loading state and offers retry after readback failure', async () => {
    const element = tagName => ({ tagName, isConnected: true, children: [], attributes: {}, listeners: {},
        set innerHTML(_value) { this.children = []; },
        setAttribute(name, value) { this.attributes[name] = value; },
        appendChild(child) { this.children.push(child); },
        addEventListener(name, listener) { this.listeners[name] = listener; } });
    const main = element('main');
    const first = deferred(), retry = deferred();
    const gates = [first, retry];
    let reads = 0;
    const context = contextFor(['renderCards', 'findModule'], {
        document: { getElementById: () => main, createElement: element },
        state: { modules: [{ name: 'Effects', controls: [], children: [] }] }, selectedModule: 'Effects',
        helioEffectsStale: true, HELIO_VIEW: '@helio', PRESETS_VIEW: '@presets', HELIO_PLAYLISTS_VIEW: '@playlists',
        isHelioFirmware: () => false, deviceFirmwareInfo: () => ({}), setHelioDeviceMode() {}, setHelioProductMode() {},
        helioTrace: { stop() {} }, closeHelioFloatingPreview() {},
        ensureHelioEffectsState: () => gates[reads++].promise,
    });
    context.renderCards();
    assert.equal(main.attributes['aria-busy'], 'true');
    assert.equal(main.children.length, 1);
    assert.equal(main.children[0].attributes.role, 'status');
    assert.equal(main.children[0].textContent, 'Loading live scene controls…');
    assert.ok(main.children.every(child => !['input', 'select'].includes(child.tagName)));
    first.resolve(false);
    await until(() => main.children.length === 2);
    assert.equal(main.attributes['aria-busy'], 'false');
    assert.equal(main.children[1].textContent, 'Retry');
    main.children[1].listeners.click();
    assert.equal(reads, 2);
    assert.equal(main.attributes['aria-busy'], 'true');
    assert.equal(main.children.length, 1);
    retry.resolve(false);
    await until(() => main.children.length === 2);
});

test('native card selection stops a playlist and invalidates live Effects without changing stored card controls', () => {
    const fixture = effectsReadbackFixture();
    let stopped = 0, callbacks;
    Object.assign(fixture.context, {
        helioPresetCollection: null, apiFetch() {}, createPresetClient: () => ({}),
        HelioPresetCards: class { constructor(options) { callbacks = options; } mount() {} },
        stopHelioPlaylist: () => { ++stopped; },
    });
    vm.runInContext(functionSource('mountHelioPresetCards'), fixture.context);
    fixture.context.mountHelioPresetCards({});
    const record = { editor: { document: { Effects: { Layer: { Scene: { speed: 91 } } } } } };
    const stored = JSON.stringify(record.editor.document);
    callbacks.onSelect(record);
    assert.equal(stopped, 1);
    assert.equal(fixture.context.helioEffectsStale, true);
    assert.equal(fixture.context.helioEffectsGeneration, 1);
    assert.equal(fixture.context.helioActiveEffect(), null);
    assert.equal(JSON.stringify(record.editor.document), stored);
});

function bootstrapFixture() {
    let now = 0, nextTimer = 1, fullReads = 0, revealed = 0;
    const timers = new Map(), sockets = [], renders = [], starts = [];
    const schedule = (callback, delay, interval = false) => {
        const id = nextTimer++;
        timers.set(id, { callback, due: now + delay, delay, interval });
        return id;
    };
    class FakeWebSocket {
        static OPEN = 1;
        constructor(url) { this.url = url; this.readyState = 0; this.sent = []; this.partial = ''; sockets.push(this); starts.push('ws'); }
        open() { this.readyState = 1; this.onopen(); }
        send(data) { this.sent.push(data); }
        close() { this.readyState = 3; }
        disconnect() { this.readyState = 3; this.onclose(); }
        fragment(text, final = false) {
            // The browser dispatches one complete message only after the final WS frame.
            this.partial += text;
            if (final) { const data = this.partial; this.partial = ''; this.onmessage({ data }); }
        }
    }
    const context = contextFor(['init', 'connectWs', 'renderStateSnapshot', 'revealInitialUi'], {
        state: null, helioBatteryConnected: true, ws: null, wsReconnectTimer: null, wsHeartbeat: null, wsPaused: false, wsUnloading: false,
        wsRetryMs: 250, WS_RETRY_MIN_MS: 250, WS_RETRY_MAX_MS: 5000, helioSceneResyncExpected: false,
        initialUiRevealed: false, selectedModule: null, HELIO_VIEW: '@helio', theme: 'dark',
        location: { host: 'helio.test' }, WebSocket: FakeWebSocket,
        setTimeout: (callback, delay) => schedule(callback, delay), clearTimeout: id => timers.delete(id),
        setInterval: (callback, delay) => schedule(callback, delay, true), clearInterval: id => timers.delete(id),
        applyTheme() {}, setupStatusBarButtons() {}, setupUpdateBadge() {}, setupCardsResize() {},
        preview: { init() { starts.push('preview'); }, setupLayout() {}, onFrame() {} },
        updateHelioLivePreview() {}, connectPreviewSocket() {}, checkUiRuntimeVersion() {},
        stopPreviewSocket() {}, clearHelioLivePreview() {}, setWsDot() {}, syncHelioBatteryStatus() {},
        API_FETCH_TIMEOUT_MS: 25000,
        refetchState() { ++fullReads; return Promise.resolve(true); },
        apiFetch() { assert.fail('initial bootstrap must not start a duplicate HTTP state request'); },
        deviceFirmwareInfo: () => ({}), isHelioFirmware: () => true,
        applyBranding: () => renders.push('branding'), renderNav: () => renders.push('nav'),
        renderCards: () => renders.push('cards'), updateValues: () => renders.push('values'),
        requestAnimationFrame: callback => callback(),
        document: { documentElement: { classList: { remove(name) { assert.equal(name, 'ui-booting'); ++revealed; } } } },
    });
    const advance = duration => {
        const end = now + duration;
        for (;;) {
            const due = [...timers].filter(([, timer]) => timer.due <= end).sort((a, b) => a[1].due - b[1].due)[0];
            if (!due) break;
            const [id, timer] = due;
            now = timer.due;
            if (timer.interval) timer.due += timer.delay;
            else timers.delete(id);
            timer.callback();
        }
        now = end;
    };
    return { context, sockets, renders, starts, advance, fullReads: () => fullReads, revealed: () => revealed };
}

test('HELIO bootstrap waits for a slow fragmented WebSocket snapshot without a duplicate HTTP fetch or render', async () => {
    const fixture = bootstrapFixture();
    await fixture.context.init();
    assert.deepEqual(fixture.starts, ['ws', 'preview']);
    assert.equal(fixture.sockets.length, 1);
    const socket = fixture.sockets[0];
    socket.open();
    assert.deepEqual(JSON.parse(socket.sent[0]), { subscribe: ['*'], detail: 'values' });
    const snapshot = JSON.stringify({ modules: [{ name: 'Effects', controls: [], children: [] }] });
    socket.fragment(snapshot.slice(0, 10));
    fixture.advance(1300);
    assert.equal(fixture.fullReads(), 0);
    assert.equal(fixture.context.state, null);
    assert.deepEqual(fixture.renders, []);
    socket.fragment(snapshot.slice(10, 30));
    fixture.advance(19500); // 20.8 seconds until FIN, matching the slow first-state case
    assert.equal(fixture.fullReads(), 0);
    assert.equal(fixture.context.state, null);
    socket.fragment(snapshot.slice(30), true);
    assert.deepEqual(plain(fixture.context.state), JSON.parse(snapshot));
    assert.deepEqual(fixture.renders, ['branding', 'nav', 'cards', 'values']);
    assert.equal(fixture.revealed(), 1);
    assert.equal(fixture.fullReads(), 0);
});

test('HELIO reconnects during an incomplete first snapshot and starts from the new socket without HTTP fallback', async () => {
    const fixture = bootstrapFixture();
    await fixture.context.init();
    const first = fixture.sockets[0];
    first.open();
    const obsolete = JSON.stringify({ modules: [{ name: 'Old socket', controls: [] }] });
    first.fragment(obsolete.slice(0, 15));
    fixture.advance(1500);
    first.disconnect();
    assert.equal(fixture.context.helioBatteryConnected, false);
    fixture.advance(250);
    assert.equal(fixture.sockets.length, 2);
    const replacement = fixture.sockets[1];
    replacement.open();
    assert.deepEqual(JSON.parse(replacement.sent[0]), { subscribe: ['*'], detail: 'values' });
    first.fragment(obsolete.slice(15), true); // an already queued callback from the closed socket is ignored
    assert.equal(fixture.context.state, null);
    const latest = JSON.stringify({ modules: [{ name: 'Effects', controls: [], children: [] }] });
    replacement.fragment(latest.slice(0, 10));
    fixture.advance(20800);
    assert.equal(fixture.context.state, null);
    assert.equal(fixture.fullReads(), 0);
    assert.deepEqual(fixture.renders, []);
    replacement.fragment(latest.slice(10), true);
    assert.equal(fixture.context.helioBatteryConnected, true);
    assert.deepEqual(plain(fixture.context.state), JSON.parse(latest));
    assert.deepEqual(fixture.renders, ['branding', 'nav', 'cards', 'values']);
    assert.equal(fixture.revealed(), 1);
    assert.equal(fixture.fullReads(), 0);
});

test('an initial compatibility compact notice requests its schema once without starting a timer fallback', async () => {
    const fixture = bootstrapFixture();
    await fixture.context.init();
    const socket = fixture.sockets[0];
    socket.open();
    socket.fragment(JSON.stringify({ compact: true, modules: [{ name: 'Effects', enabled: true }] }), true);
    assert.equal(fixture.fullReads(), 1);
    assert.equal(fixture.context.state, null);
    assert.deepEqual(fixture.renders, []);
    fixture.advance(20800);
    assert.equal(fixture.fullReads(), 1);
});

function moduleMetadataFixture() {
    const values = { detail: 'values', modules: [{ name: 'Drivers', type: 'Drivers', controls: [
        { name: 'brightness', type: 'uint8', value: 32 }, { name: 'palette', type: 'palette', value: 'Customer palette' },
    ] }] };
    const gates = [], requests = [];
    const context = contextFor(['findModule', 'replaceModuleInState', 'allModules', 'applyStatePatch',
        'helioMetadataRoot', 'helioNeedsModuleMetadata', 'mergeHelioModuleMetadata', 'ensureHelioModuleMetadata'], {
        state: values, readModuleData(name) {
            requests.push(name); const gate = deferred(); gates.push(gate); return gate.promise;
        },
    });
    return { context, gates, requests, values };
}

test('lazy metadata adds real options while retaining a newer live patch received during the HTTP read', async () => {
    const fixture = moduleMetadataFixture();
    assert.equal(fixture.context.helioNeedsModuleMetadata('Drivers'), true);
    assert.deepEqual(fixture.requests, []);
    const first = fixture.context.ensureHelioModuleMetadata('Drivers');
    const second = fixture.context.ensureHelioModuleMetadata('Drivers');
    assert.deepEqual(fixture.requests, ['Drivers']);
    fixture.context.applyStatePatch([{ path: 'Drivers/brightness', value: 91 },
        { path: 'Drivers/palette', value: 'Other palette' }]);
    fixture.gates[0].resolve({ name: 'Drivers', type: 'Drivers', controls: [
        { name: 'brightness', type: 'uint8', value: 32, min: 0, max: 255 },
        { name: 'palette', type: 'palette', value: 'Customer palette', options: ['Customer palette', 'Other palette'] },
    ] });
    assert.equal(await first, true);
    assert.equal(await second, true);
    const controls = fixture.context.state.modules[0].controls;
    assert.equal(controls[0].value, 91);
    assert.equal(controls[0].max, 255);
    assert.equal(controls[1].value, 'Other palette');
    assert.deepEqual(controls[1].options, ['Customer palette', 'Other palette']);
    assert.equal(fixture.context.helioNeedsModuleMetadata('Drivers'), false);
});

test('a new values snapshot supersedes an in-flight schema result and failed metadata can be retried', async () => {
    const fixture = moduleMetadataFixture();
    const loading = fixture.context.ensureHelioModuleMetadata('Drivers');
    fixture.context.helioMetadataGeneration++;
    fixture.context.state = { detail: 'values', modules: [{ name: 'Drivers', type: 'Drivers', controls: [
        { name: 'brightness', type: 'uint8', value: 91 },
    ] }] };
    fixture.gates[0].reject(new Error('obsolete HTTP failure'));
    await until(() => fixture.requests.length === 2);
    fixture.gates[1].reject(new Error('current HTTP failure'));
    assert.equal(await loading, false);
    assert.equal(fixture.context.state.modules[0].controls[0].value, 91);
    assert.equal(fixture.context.helioNeedsModuleMetadata('Drivers'), true);
    const retry = fixture.context.ensureHelioModuleMetadata('Drivers');
    fixture.gates[2].resolve({ name: 'Drivers', type: 'Drivers', controls: [
        { name: 'brightness', type: 'uint8', value: 91, min: 0, max: 255 },
    ] });
    assert.equal(await retry, true);
    assert.equal(fixture.context.helioNeedsModuleMetadata('Drivers'), false);
});

test('values-only bootstrap does not open the preview until its first state is usable or request full REST state', async () => {
    const fixture = bootstrapFixture();
    let previews = 0;
    fixture.context.connectPreviewSocket = () => { ++previews; fixture.context.previewWs = {}; };
    await fixture.context.init();
    const socket = fixture.sockets[0];
    socket.open();
    assert.equal(previews, 0);
    fixture.advance(1500);
    assert.equal(fixture.fullReads(), 0);
    socket.fragment(JSON.stringify({ detail: 'values', modules: [
        { name: 'Drivers', type: 'Drivers', controls: [{ name: 'brightness', type: 'uint8', value: 91 }] },
    ] }), true);
    assert.equal(previews, 1);
    assert.equal(fixture.context.state.detail, 'values');
    assert.equal(fixture.context.state.modules[0].controls[0].value, 91);
    assert.equal(fixture.revealed(), 1);
    assert.equal(fixture.fullReads(), 0);
});

test('the product app hands its unconsumed initial response to exactly one client before mounting cards', () => {
    const pending = Promise.resolve({ status: 200 });
    const lifecycle = { finish() {}, cancel() {} };
    const options = [], mounts = [];
    const context = contextFor(['acceptInitialPresetIndexResponse', 'takeInitialPresetIndexResponse', 'mountHelioPresetCards'], {
        helioPresetCollection: null, initialPresetIndex: null, apiFetch() {},
        createPresetClient(configuration) { options.push(configuration); return {}; },
        HelioPresetCards: class { constructor() {} mount(host) { mounts.push(host); } },
    });
    assert.equal(context.acceptInitialPresetIndexResponse(pending, lifecycle), true);
    assert.equal(context.acceptInitialPresetIndexResponse(Promise.resolve({ status: 404 })), false);
    const first = {}, second = {};
    context.mountHelioPresetCards(first);
    assert.equal(options.length, 1);
    assert.equal(options[0].initialIndexResponse, pending);
    assert.equal(options[0].initialIndexLifecycle, lifecycle);
    assert.equal(context.initialPresetIndex, null);
    context.mountHelioPresetCards(second);
    assert.equal(options.length, 1);
    assert.deepEqual(mounts, [first, second]);
    assert.equal(context.acceptInitialPresetIndexResponse(pending), false);
    assert.deepEqual(plain(context.takeInitialPresetIndexResponse()), {});
});

function earlyCardsFixture({ firmware = 'helio1', initialState = null, unloading = false, listing = null } = {}) {
    const node = tagName => ({ tagName, className: '', id: '', dataset: {}, style: {}, children: [], attributes: {},
        listeners: {}, isConnected: true, _html: '',
        classList: { add() {}, remove() {}, toggle() {} },
        setAttribute(name, value) { this.attributes[name] = value; },
        append(...children) { for (const child of children) this.appendChild(child); },
        appendChild(child) { child.parentElement = this; this.children.push(child); return child; },
        insertBefore(child, anchor) { child.parentElement = this; this.children.splice(this.children.indexOf(anchor), 0, child); },
        replaceChildren(...children) { this.children = []; this.append(...children); },
        remove() { this.parentElement.children = this.parentElement.children.filter(child => child !== this); this.isConnected = false; },
        contains(target) { return this === target || this.children.some(child => child.contains?.(target)); },
        addEventListener(name, callback) { this.listeners[name] = callback; },
        querySelector(selector) {
            const className = /\.([\w-]+)/.exec(selector)?.[1];
            const matches = child => className ? child.className.split(' ').includes(className) : child.tagName === selector;
            const direct = this.children.find(matches);
            if (direct || selector.startsWith(':scope')) return direct || null;
            return this.children.map(child => child.querySelector?.(selector)).find(Boolean) || null;
        },
        get innerHTML() { return this._html; },
        set innerHTML(value) { this._html = value; this.children = []; },
    });
    const main = node('main'), document = { activeElement: null, createElement: node,
        getElementById: id => id === 'main' ? main : null,
        documentElement: { classList: { remove() {} } } };
    const phases = [], clients = [], mounts = [], delayedRenderer = [], requests = [], applyGates = [], writeGates = [];
    const presetSource = readFileSync(new URL('../../src/ui/helio-presets.js', import.meta.url), 'utf8');
    const native = vm.runInNewContext(presetSource.replace(/^export /gm, '') + '\n({createPresetClient,PresetEditor});',
        { setTimeout, clearTimeout, fetch() { assert.fail('all requests use the injected transport'); } });
    const cardsSource = readFileSync(new URL('../../src/ui/helio-preset-cards.js', import.meta.url), 'utf8')
        .replace(/^import[^\n]+\n/gm, '').replace('export class HelioPresetCards', 'class HelioPresetCards');
    const Cards = vm.runInNewContext(cardsSource + '\nHelioPresetCards;', { PresetEditor: native.PresetEditor, document });
    const doc = name => ({ Effects: { Layer: { Scene: { speed: 1, enabled: true } } },
        $customer: { name }, $origin: { scene: 'ripple', catalog: 'saved-catalog' },
        $uiSchema: { effectType: 'RingsEffect', effectPath: ['Effects', 'Layer', 'Scene'],
            controls: [{ name: 'speed', type: 'uint8', min: 1, max: 255, default: 30 }] } });
    const response = (result, status = 200, etag = null) => ({ ok: status >= 200 && status < 300, status,
        headers: { get: name => name === 'ETag' ? etag : null }, json: async () => result });
    const index = listing || Promise.resolve(response({ items: ['A', 'B', 'C'].map(name => ({ name, kind: 'look',
        etag: name, roles: ['effects'], slot: null, originScene: 'ripple' })), active: { effects: null }, limit: 64 }));
    const identity = { name: 'Firmware', type: 'FirmwareUpdateModule', controls: [
        { name: 'firmware', value: firmware }, { name: 'version', value: 'v0.1.0-test' },
    ] };
    let context;
    context = contextFor(['acceptInitialPresetIndexResponse', 'takeInitialPresetIndexResponse', 'deviceFirmwareInfo',
        'isNebuliteFirmware', 'isHelioFirmware', 'findModule', 'renderEarlyHelioHome', 'renderHelioConnectingStage',
        'renderHelioHome', 'hydrateHelioHome', 'renderStateSnapshot', 'mountHelioPresetCards', 'helioPresetModule',
        'invalidateHelioEffects'], {
        state: initialState, wsUnloading: unloading, initialPresetIndex: null, initialFirmwareModule: null,
        earlyHelioHome: null, helioPresetCollection: null, helioPresetModules: new Map(), HELIO_VIEW: '@helio',
        selectedModule: null, initialUiRevealed: false, helioEffectsGeneration: 0, helioEffectsStale: false,
        helioModeGeneration: 0, helioModeApplying: false, helioModeApplyingId: '', helioModeQueued: null,
        document, performance: { now: () => 1 }, setTimeout() {},
        apiFetch(url, options = {}) {
            requests.push({ url, options });
            const name = decodeURIComponent(url.split('/')[3] || '');
            if (options.method === 'PUT') { const gate = deferred(); writeGates.push(gate); return gate.promise; }
            if (options.method === 'POST') { const gate = deferred(); applyGates.push({ name, ...gate }); return gate.promise; }
            if (url.startsWith('/api/presets/')) return Promise.resolve(response(doc(name), 200, `"${name}-etag"`));
            if (url.startsWith('/api/preset-templates/')) return Promise.resolve(response(doc('template')));
            assert.fail(`unexpected bootstrap request ${url}`);
        },
        createPresetClient(options) { const client = native.createPresetClient(options); clients.push(client); return client; },
        HelioPresetCards: class extends Cards { mount(host) { mounts.push(host); return super.mount(host); } },
        applyBranding() { phases.push('branding'); }, setHelioDeviceMode() {}, setHelioProductMode() {},
        revealInitialUi() { context.initialUiRevealed = true; },
        renderNav() { assert.notEqual(context.state, null); phases.push('nav'); },
        renderCards() { phases.push('normal-render'); main.innerHTML = ''; }, updateValues() {},
        deferHelioTraceUntilCards(mounted, host) { assert.notEqual(context.state, null); delayedRenderer.push({ mounted, host }); },
        renderHelioControlPanel(module, options = {}) {
            const panel = node('section');
            if (options.sceneOnly) {
                const input = node('input'); input.value = String(module.controls.find(control => control.name === 'speed').value);
                panel.appendChild(input);
            } else { assert.notEqual(context.state, null); phases.push('runtime-controls'); }
            return panel;
        },
        renderHelioSetupMenu() { assert.notEqual(context.state, null); phases.push('setup'); return node('aside'); },
        helioDrivers: () => context.findModule('Drivers'), helioControl: () => null,
        controlValue: (module, key, fallback) => module?.controls?.find(control => control.name === key)?.value ?? fallback,
        helioControlsOpen: () => false, toggleHelioControls() {}, openNavDrawer() {}, openHelioFloatingPreview() {},
        makeHelioTiltToggle: () => node('button'), makeHelioSwivelToggle: () => node('button'), makeHelioWallToggle: () => node('button'),
        syncHelioBatteryStatus() { assert.notEqual(context.state, null); phases.push('battery'); },
        refreshHelioTrace() { assert.notEqual(context.state, null); phases.push('trace'); },
        startHelioModePreviews() { assert.notEqual(context.state, null); phases.push('previews'); },
        stopHelioPlaylist() {}, clearHelioLivePreview() { assert.notEqual(context.state, null); },
        updateHelioHome() { assert.notEqual(context.state, null); },
        helioNeedsModuleMetadata: () => false,
    });
    const snapshot = (actualFirmware = firmware) => ({ modules: [
        { ...identity, controls: [{ name: 'firmware', value: actualFirmware }, { name: 'version', value: 'v0.2.0-live' }] },
        { name: 'Drivers', controls: [{ name: 'on', value: false }, { name: 'brightness', value: 17 }] },
        { name: 'Effects', children: [] },
    ] });
    return { context, document, main, identity, index, clients, mounts, delayedRenderer, phases, requests, applyGates,
        writeGates, response, snapshot };
}

function selectedEffect(document) { return document.Effects.Layer.Scene; }

test('verified early HELIO cards use the real index while runtime remains absent and hydrate the same focused draft', async () => {
    const f = earlyCardsFixture();
    assert.equal(f.context.acceptInitialPresetIndexResponse(f.index, null, f.identity), true);
    const early = f.context.earlyHelioHome;
    assert.ok(early);
    const reservedPanels = early.wrap.querySelector(':scope > .helio-panels');
    assert.ok(reservedPanels);
    assert.equal(reservedPanels.children.length, 0);
    assert.equal(early.wrap.children.indexOf(reservedPanels) + 1, early.wrap.children.indexOf(early.presets));
    await early.mounted;
    assert.equal(f.context.state, null); assert.equal(f.mounts.length, 1); assert.equal(f.clients.length, 1);
    assert.equal(f.context.helioPresetCollection.records.size, 3);
    assert.deepEqual(f.phases, ['branding']); assert.equal(f.delayedRenderer.length, 0);
    assert.equal(early.wrap.querySelector(':scope > .helio-stage').querySelector('button'), null,
        'connecting stage exposes no guessed runtime actions while actual preset buttons remain usable');
    const cards = f.context.helioPresetCollection, record = cards.records.get('A');
    record.details.open = true;
    await cards.openControls(record, record.details, record.content);
    const editor = record.editor, input = record.content.querySelector('input');
    assert.ok(input); f.document.activeElement = input;
    input.value = '77'; input.selectionStart = input.selectionEnd = 2;
    editor.edit(['Effects', 'Layer', 'Scene'], 'speed', 77);
    const saving = editor.flush();
    await until(() => f.writeGates.length === 1);
    const put = f.requests.find(request => request.options.method === 'PUT');
    assert.equal(put.options.headers['If-Match'], '"A-etag"');
    f.context.state = f.snapshot();
    f.context.renderStateSnapshot();
    assert.equal(f.main.children[0], early.wrap); assert.equal(record.content.contains(input), true);
    assert.equal(early.wrap.querySelector(':scope > .helio-panels'), reservedPanels);
    assert.equal(reservedPanels.children.length, 1);
    assert.equal(early.wrap.children.indexOf(reservedPanels) + 1, early.wrap.children.indexOf(early.presets));
    assert.equal(f.document.activeElement, input); assert.equal(input.value, '77'); assert.equal(input.selectionStart, 2);
    assert.equal(cards.records.get('A'), record); assert.equal(record.editor, editor);
    assert.equal(selectedEffect(editor.document).speed, 77); assert.equal(editor.dirty, true);
    assert.equal(f.mounts.length, 1); assert.equal(f.clients.length, 1);
    assert.equal(f.delayedRenderer.length, 1); assert.equal(f.delayedRenderer[0].mounted, early.mounted);
    assert.equal(f.delayedRenderer[0].host, early.presets);
    assert.equal(f.context.initialFirmwareModule, null);
    f.writeGates[0].resolve(f.response({ stored: true, etag: 'A-new' }, 200, '"A-new"'));
    await saving;
    assert.equal(editor.etag, '"A-new"'); assert.equal(editor.dirty, false);
    assert.equal(f.requests.filter(request => request.options.method === 'PUT').length, 1);
    assert.equal(f.requests.filter(request => request.url === '/api/presets').length, 0);
    for (const item of cards.records.values()) item.editor?.dispose();
});

for (const kind of ['nebulite-s3', 'esp32', 'unloading', 'state-first']) {
    test(`early native cards cannot override ${kind} routing or a real state that won the race`, () => {
        const f = earlyCardsFixture({ firmware: kind === 'nebulite-s3' || kind === 'esp32' ? kind : 'helio1',
            unloading: kind === 'unloading', initialState: kind === 'state-first' ? { modules: [] } : null });
        assert.equal(f.context.acceptInitialPresetIndexResponse(f.index, null, f.identity), true);
        assert.equal(f.context.earlyHelioHome, null); assert.equal(f.mounts.length, 0);
        assert.equal(f.clients.length, 0); assert.equal(f.main.children.length, 0);
        assert.equal(f.context.state === null, kind !== 'state-first');
    });
}

for (const outcome of ['ok', 'conflict', 'error']) {
    test(`early native Play preserves latest intent across the first snapshot and ${outcome}`, async () => {
        const f = earlyCardsFixture();
        f.context.acceptInitialPresetIndexResponse(f.index, null, f.identity);
        await f.context.earlyHelioHome.mounted;
        const cards = f.context.helioPresetCollection;
        const first = cards.play(cards.records.get('A'));
        await until(() => f.applyGates.length === 1);
        cards.play(cards.records.get('B')); cards.play(cards.records.get('C'));
        assert.equal(f.context.state, null); assert.equal(cards.selected, 'C');
        const generation = f.context.helioEffectsGeneration;
        f.context.state = f.snapshot(); f.context.renderStateSnapshot();
        assert.equal(f.context.helioEffectsGeneration, generation); assert.equal(f.context.helioEffectsStale, true);
        assert.equal(cards.selected, 'C'); assert.equal(cards.applying, true);
        f.applyGates[0].resolve(f.response({ applied: true, outputEtag: 'a'.repeat(64) }, 200, '"A-etag"'));
        await until(() => f.applyGates.length === 2);
        assert.equal(f.applyGates[1].name, 'C');
        f.applyGates[1].resolve(outcome === 'ok'
            ? f.response({ applied: true, outputEtag: 'c'.repeat(64) }, 200, '"C-etag"')
            : f.response({ error: outcome === 'conflict' ? 'output changed' : 'unavailable' }, outcome === 'conflict' ? 412 : 503));
        await first;
        assert.equal(cards.applying, false); assert.equal(f.applyGates.length, 2);
        assert.equal(cards.selected, outcome === 'ok' ? 'C' : null);
        assert.equal(cards.records.get('C').outputEtag, outcome === 'ok' ? 'c'.repeat(64) : null);
        assert.equal(f.mounts.length, 1);
        for (const item of cards.records.values()) item.editor?.dispose();
    });
}

test('the first real snapshot can hydrate an early host while its sole canonical index is still pending', async () => {
    const index = deferred(), f = earlyCardsFixture({ listing: index.promise });
    f.context.acceptInitialPresetIndexResponse(index.promise, null, f.identity);
    const early = f.context.earlyHelioHome;
    assert.equal(f.context.helioPresetCollection.records.size, 0);
    f.context.state = f.snapshot(); f.context.renderStateSnapshot();
    assert.equal(f.main.children[0], early.wrap); assert.equal(f.mounts.length, 1);
    assert.equal(f.delayedRenderer[0].mounted, early.mounted);
    index.resolve(f.response({ items: [{ name: 'A', kind: 'look', roles: ['effects'], slot: null, etag: 'A' }],
        active: { effects: null }, limit: 64 }));
    await early.mounted;
    assert.equal(f.context.helioPresetCollection.records.size, 1);
    assert.equal(f.mounts.length, 1); assert.equal(f.clients.length, 1);
    assert.equal(f.requests.length, 0);
});

test('a real non-HELIO first snapshot wins over the provisional verified branding and does not hydrate guessed geometry', async () => {
    const f = earlyCardsFixture();
    f.context.acceptInitialPresetIndexResponse(f.index, null, f.identity);
    await f.context.earlyHelioHome.mounted;
    f.context.state = f.snapshot('esp32'); f.context.renderStateSnapshot();
    assert.equal(f.context.deviceFirmwareInfo().firmware, 'esp32');
    assert.equal(f.context.initialFirmwareModule, null); assert.equal(f.context.earlyHelioHome, null);
    assert.equal(f.main.children.length, 0); assert.equal(f.delayedRenderer.length, 0);
    assert.ok(f.phases.includes('normal-render'));
    assert.equal(f.phases.includes('runtime-controls'), false);
});

test('early palette metadata hooks complete false without creating a state waiter or fetching runtime controls', async () => {
    const f = earlyCardsFixture();
    f.context.acceptInitialPresetIndexResponse(f.index, null, f.identity);
    await f.context.earlyHelioHome.mounted;
    const before = f.requests.length;
    const result = await f.context.helioPresetCollection.onControls({ editor: { document: {
        $uiSchema: { controls: [{ name: 'palette', type: 'palette' }] }, Drivers: { palette: 'Customer palette' },
    } } });
    assert.equal(result, false); assert.equal(f.context.state, null); assert.equal(f.requests.length, before);
    assert.equal(f.mounts.length, 1);
});
