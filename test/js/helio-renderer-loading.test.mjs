import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const source = readFileSync(new URL('../../src/ui/helio-app.js', import.meta.url), 'utf8');
function functionSource(name) {
    const match = new RegExp(`^(?:async )?function ${name}\\(`, 'm').exec(source);
    assert.ok(match, `${name} exists`);
    return source.slice(match.index, source.indexOf('\n}', match.index) + 2);
}
function deferred() {
    let resolve, reject;
    const promise = new Promise((done, fail) => { resolve = done; reject = fail; });
    return { promise, resolve, reject };
}
async function settle() { for (let i = 0; i < 15; ++i) await Promise.resolve(); }
test('NEBULITE scene cards do not download the sphere renderer', async () => {
    const f = fixture();
    f.context.isNebuliteFirmware = () => true;
    f.context.deferHelioTraceUntilCards(Promise.resolve(), { isConnected: true });
    await settle();
    assert.equal(f.frames.length, 0);
    assert.equal(f.imports.length, 0);
});
test('NEBULITE paints a scene card without loading the sphere renderer', () => {
    const calls = [];
    const context = vm.createContext({
        isNebuliteFirmware: () => true, drawHelioTraceWebGLPreview: null,
        helioCardPreviewTime: () => 1, paintHelioScenePreview: (canvas, time) => calls.push([canvas, time]),
    });
    vm.runInContext(functionSource('paintHelioPreviewItem'), context);
    const canvas = {};
    context.paintHelioPreviewItem({ type: 'helio', canvas }, 100);
    assert.deepEqual(calls, [[canvas, 1]]);
    context.isNebuliteFirmware = () => false;
    context.paintHelioPreviewItem({ type: 'helio', canvas }, 100);
    assert.equal(calls.length, 1);
});
test('NEBULITE preview count follows the product grid instead of the sphere default', () => {
    const grid = { controls: [{ name: 'width', value: 91 }] };
    const context = vm.createContext({
        isNebuliteFirmware: () => true,
        firstModuleByType: type => type === 'GridLayout' ? grid : null,
        controlValue: (module, name, fallback) => module?.controls?.find(item => item.name === name)?.value ?? fallback,
        HELIO_DEFAULT_LED_COUNT: 222,
    });
    vm.runInContext([functionSource('helioPrimaryLayout'), functionSource('helioCurrentCount')].join('\n'), context);
    assert.equal(context.helioCurrentCount(), 91);
    grid.controls[0].value = 9;
    assert.equal(context.helioCurrentCount(), 9);
});
test('NEBULITE counts actual mapped outputs and all grid dimensions', () => {
    const map = {type:'PixelMapLayout',controls:[{name:'points',value:'1,8;12,8;10,3;16,2;4,0;5,8;13,4;16,8;8,4'}]};
    const grid = {type:'GridLayout',controls:[{name:'width',value:7},{name:'height',value:13},{name:'depth',value:1}]};
    let active = map;
    const context = vm.createContext({isNebuliteFirmware:()=>true,
        firstModuleByType:type=>active.type===type?active:null,
        controlValue:(module,name,fallback)=>module?.controls?.find(item=>item.name===name)?.value??fallback,
        HELIO_DEFAULT_LED_COUNT:222});
    vm.runInContext([functionSource('helioPrimaryLayout'),functionSource('helioCurrentCount')].join('\n'),context);
    assert.equal(context.helioCurrentCount(),9);
    map.controls[0].value=Array.from({length:31},()=> '0,0').join(';');
    assert.equal(context.helioCurrentCount(),31);
    map.controls[0].value='0,0;garbage';assert.equal(context.helioCurrentCount(),222);
    active=grid;assert.equal(context.helioCurrentCount(),91);
});
function element() {
    return { isConnected: true, dataset: {}, children: [], listeners: {}, textContent: '',
        setAttribute() {}, appendChild(child) { child.parentElement = this; this.children.push(child); },
        replaceChildren(...children) { this.children = children; }, contains() { return false; },
        querySelector(selector) {
            return this.children.find(child => selector.includes('helio-preview-load-status')
                ? child.className?.includes('helio-preview-load-status') : child.tagName === 'canvas') || null;
        },
        addEventListener(name, callback) { this.listeners[name] = callback; },
        remove() { this.parentElement.children = this.parentElement.children.filter(child => child !== this); },
    };
}
function fixture() {
    const pending = deferred(), frames = [], draws = [], imports = [], paints = [];
    let currentCanvas;
    const makeCanvas = () => {
        const canvas = element(), parent = element();
        canvas.tagName = 'canvas';
        parent.appendChild(canvas);
        return canvas;
    };
    const canvas = currentCanvas = makeCanvas();
    const trace = {
        setSampler(sampler) { this.sampler = sampler; }, setLedCount(count) { this.count = count; },
        setExposure() {}, setDetail() {}, mount(target) { this.canvas = target; return true; },
        draw() { draws.push({ canvas: this.canvas, rgb: Array.from(this.sampler(0)), count: this.count }); },
        cameraAnimationActive: () => false, start() {}, stop() {},
    };
    const module = { HelioTrace: class {}, helioTrace: trace, drawHelioTraceWebGLPreview() {} };
    const context = vm.createContext({
        console: { warn() {} }, Promise, state: { modules: [] }, helioTrace: null, HelioTrace: null, drawHelioTraceWebGLPreview: null,
        helioTraceLoadPromise: null, helioTraceLoadFailed: false, wsUnloading: false,
        helioFloatingPreviewEl: null, helioFloatingTrace: null, helioLivePreviewDrawAt: 0,
        helioLivePreviewRgb: new Uint8Array([12, 34, 56]), helioLivePreviewCount: 1,
        helioLivePreviewSeenAt: 100, helioLivePreviewLevel: 56,
        performance: { now: () => 100 }, requestAnimationFrame: callback => frames.push(callback),
        loadHelioTraceModule: () => { imports.push('import'); return pending.promise; },
        helioPreviewDetail: () => ({}), helioPreviewWallEnabled: () => false,
        helioCurrentCount: () => 222, helioPreviewNearViewport: () => true,
        isNebuliteFirmware: () => false,
        startHelioModePreviews: () => paints.push('current cards'),
        document: {
            body: { classList: { contains: () => true } },
            getElementById: () => currentCanvas,
            createElement(tagName) { const node = element(); node.tagName = tagName; return node; },
        },
    });
    const names = ['requestHelioTrace', 'deferHelioTraceUntilCards', 'updateHelioTraceLoading',
        'configureHelioTraceRenderer', 'drawHelioMainPreview', 'ensureHelioFloatingTrace',
        'syncHelioFloatingPreview', 'updateHelioTraceLiveStats', 'refreshHelioTrace',
        'helioLivePreviewFresh', 'sampleHelioLivePreview', 'drawHelioLivePreviewFrame'];
    vm.runInContext(names.map(functionSource).join('\n'), context);
    return { context, pending, module, frames, draws, imports, paints, canvas, makeCanvas,
        setCanvas(value) { currentCanvas = value; },
        frame() { assert.ok(frames.length); frames.shift()(100); },
        status(target = currentCanvas) { return target.parentElement.querySelector('.helio-preview-load-status'); },
    };
}

test('the renderer is a dynamic import and the WSP decoder stays on the initial app path', () => {
    assert.doesNotMatch(source, /^import[^\n]*from ["']\.\/heliotrace\.js["']/m);
    assert.match(functionSource('loadHelioTraceModule'), /return import\("\.\/heliotrace\.js"\)/);
    assert.match(source, /^import \{ preview \} from "\.\/helio-preview3d\.js"/m);
    assert.match(functionSource('init'), /preview\.onFrame\(updateHelioLivePreview\)/);
});

for (const failed of [false, true]) {
    test(`cards ${failed ? 'error' : 'success'} paints before the first renderer import`, async () => {
        const f = fixture(), mounted = deferred(), host = element();
        f.context.deferHelioTraceUntilCards(mounted.promise, host);
        await settle();
        assert.equal(f.frames.length, 0);
        assert.equal(f.imports.length, 0);
        if (failed) mounted.reject(new Error('index unavailable'));
        else mounted.resolve();
        await settle();
        assert.equal(f.frames.length, 1);
        f.frame();
        await settle();
        assert.equal(f.imports.length, 0, 'the cards have a browser paint before import');
        f.frame();
        await settle();
        assert.equal(f.imports.length, 1);
        assert.equal(f.status().textContent, 'Loading preview…');
    });
}

for (const checkpoint of ['mount', 'paint']) {
    test(`leaving the old cards during ${checkpoint} cancels automatic renderer work`, async () => {
        const f = fixture(), mounted = deferred(), host = element();
        f.context.deferHelioTraceUntilCards(mounted.promise, host);
        if (checkpoint === 'mount') host.isConnected = false;
        mounted.resolve();
        await settle();
        if (checkpoint === 'paint') { f.frame(); host.isConnected = false; f.frame(); }
        await settle();
        assert.equal(f.imports.length, 0);
    });
}

test('explicit preview shares one pending import with automatic loading and paints the latest real frame', async () => {
    const f = fixture(), mounted = Promise.resolve(), host = element();
    f.context.deferHelioTraceUntilCards(mounted, host);
    const first = f.context.requestHelioTrace();
    assert.equal(f.context.requestHelioTrace(), first);
    await settle(); f.frame(); f.frame(); await settle();
    assert.equal(f.imports.length, 1);
    assert.equal(f.draws.length, 0);
    const originalStatus = f.status();
    const next = f.makeCanvas();
    f.canvas.isConnected = false;
    f.setCanvas(next);
    f.context.helioLivePreviewRgb = new Uint8Array([71, 81, 91, 4, 5, 6]);
    f.context.helioLivePreviewCount = 2;
    f.pending.resolve(f.module);
    assert.equal(await first, true);
    assert.equal(f.draws.length, 1);
    assert.equal(f.draws[0].canvas, next, 'no retained canvas or remount');
    assert.deepEqual(f.draws[0].rgb, [71, 81, 91]);
    assert.equal(f.draws[0].count, 2);
    assert.equal(f.status(next), null);
    assert.equal(f.status(f.canvas), originalStatus, 'detached view is untouched');
    assert.deepEqual(f.paints, ['current cards']);
    await f.context.requestHelioTrace();
    assert.equal(f.imports.length, 1);
});

test('a successful import during pagehide does not touch the old canvas or schedule card painting', async () => {
    const f = fixture(), loading = f.context.requestHelioTrace();
    await settle();
    const status = f.status();
    f.context.wsUnloading = true;
    f.pending.resolve(f.module);
    assert.equal(await loading, true);
    assert.equal(f.draws.length, 0);
    assert.equal(f.status(), status);
    assert.deepEqual(f.paints, []);
    f.context.wsUnloading = false;
    f.context.refreshHelioTrace();
    assert.equal(f.draws.length, 1);
    assert.equal(f.status(), null);
});

test('a renderer fetch failure offers an explicit retry without touching controls or cards', async () => {
    const f = fixture(), loading = f.context.requestHelioTrace();
    await settle();
    f.pending.reject(new Error('503'));
    assert.equal(await loading, false);
    const status = f.status();
    assert.equal(status.textContent, 'Preview could not be loaded. ');
    const retry = status.children.find(node => node.tagName === 'button');
    assert.equal(retry.textContent, 'Retry preview');
    assert.equal(f.imports.length, 1);
    assert.equal(f.paints.length, 0);
    const second = deferred();
    f.context.loadHelioTraceModule = () => { f.imports.push('retry'); return second.promise; };
    const retried = retry.listeners.click();
    await settle();
    assert.equal(f.status().textContent, 'Loading preview…');
    assert.equal(f.context.requestHelioTrace(), retried);
    second.resolve(f.module);
    assert.equal(await retried, true);
    assert.equal(f.imports.length, 2);
    assert.equal(f.status(), null);
});

test('frames before the renderer is ready never dereference its singleton or start an import', () => {
    const f = fixture();
    f.context.helioPreviewSettings = () => ({ mainFps: 24 });
    f.context.drawHelioLivePreviewFrame(100, true);
    assert.equal(f.draws.length, 0);
    assert.equal(f.imports.length, 0);
    assert.deepEqual(Array.from(f.context.sampleHelioLivePreview(0)), [12, 34, 56]);
});

test('native card Controls and conditional Play complete while the renderer import remains pending', async () => {
    const f = fixture(), loading = f.context.requestHelioTrace();
    await settle();
    const cardsSource = readFileSync(new URL('../../src/ui/helio-preset-cards.js', import.meta.url), 'utf8')
        .replace(/^import[^\n]+\n/gm, '').replace('export class HelioPresetCards', 'class HelioPresetCards');
    const Cards = vm.runInNewContext(cardsSource + '\nHelioPresetCards;', { document: {} });
    const input = element(), writes = [];
    input.value = '1';
    const cards = new Cards({ makeControls: () => input, client: { async apply(name, options) {
        writes.push({ name, etag: options.etag });
        return { outputEtag: 'a'.repeat(64) };
    } } });
    cards.status = element();
    const record = { name: 'Rings', content: element(), editor: {
        etag: 'confirmed-etag', document: { $uiSchema: { controls: [] } }, dirty: false, async flush() {},
    } };
    cards.records.set(record.name, record);
    cards.ensureEditor = async () => record.editor;
    cards.ensureControls = async () => false;
    await cards.openControls(record, { open: true }, record.content);
    assert.equal(record.content.children[0], input);
    await cards.play(record);
    assert.equal(cards.selected, 'Rings');
    assert.equal(cards.applying, false);
    assert.deepEqual(writes, [{ name: 'Rings', etag: 'confirmed-etag' }]);
    assert.equal(f.context.helioTrace, null);
    f.pending.resolve(f.module);
    await loading;
    assert.equal(record.content.children[0], input, 'successful renderer import preserves the editor node');
    assert.equal(input.value, '1');
    assert.equal(record.editor.etag, 'confirmed-etag');
});


test('settled early cards cannot import the renderer until actual runtime state arrives', async () => {
    const f = fixture(), host = element(), mounted = Promise.resolve();
    f.context.state = null;
    f.context.deferHelioTraceUntilCards(mounted, host);
    await settle();
    while (f.frames.length) f.frame();
    await settle();
    assert.equal(f.imports.length, 0);
    assert.equal(f.draws.length, 0);
    assert.equal(f.context.state, null);
    f.context.state = { modules: [{ name: 'Layouts', children: [{ name: 'Real ring', type: 'RingLayout' }] }] };
    f.context.deferHelioTraceUntilCards(mounted, host);
    await settle();
    f.frame(); f.frame(); await settle();
    assert.equal(f.imports.length, 1);
    f.pending.resolve(f.module);
    await settle();
    assert.equal(f.draws.length, 1);
});
