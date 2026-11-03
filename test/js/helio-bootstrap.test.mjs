import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const presets = readFileSync(new URL('../../src/ui/helio-presets.js', import.meta.url), 'utf8');
const { createPresetClient } = await import(`data:text/javascript;base64,${Buffer.from(presets).toString('base64')}`);

const source = readFileSync(new URL('../../src/ui/helio-bootstrap.js', import.meta.url), 'utf8')
    .replaceAll('import(', 'load(').replace(/\nstart\(\);\s*$/, '');
function bootFixture(pathname, firmware, { broken = false, prismError = false, holdPrism = false,
    productImportFails = false, productImportGate = null, acceptIndex = true, indexError = null,
    holdHeaders = false, holdBody = false, abortSupport = true } = {}) {
    const loaded = [], assets = [], identity = [], indexRequests = [], handoffs = [], lifecycles = [], productIdentities = [];
    const firmwareModule = { name: "Firmware", type: "FirmwareModule", controls: [{ name: "firmware", value: firmware }, { name: "version", value: "v0.1.0-test" }] };
    const logo = { hidden: true }, favicon = {};
    let aborted = 0, cancelled = 0, jsonReads = 0;
    let responseSignal;
    const timers = new Map(), listeners = new Map();
    const abortError = new Error('original abort'); abortError.name = 'AbortError';
    const index = { ok: true, status: 200, headers: { get: () => null },
        body: { async cancel() { ++cancelled; } }, async json() {
            ++jsonReads;
            if (holdBody) return new Promise((_resolve, reject) => responseSignal.listeners.push(() => reject(abortError)));
            return { items: [] };
        } };
    let script;
    const context = vm.createContext({ location: { pathname },
        AbortController: abortSupport ? class {
            signal = { aborted: false, listeners: [] };
            abort() { if (this.signal.aborted) return; this.signal.aborted = true; ++aborted;
                for (const callback of this.signal.listeners) callback(); }
        } : undefined,
        setTimeout(callback, delay) { const id = timers.size + 1; timers.set(id, { callback, delay }); return id; },
        clearTimeout(id) { timers.delete(id); },
        addEventListener(name, callback) { listeners.set(name, callback); },
        removeEventListener(name, callback) { if (listeners.get(name) === callback) listeners.delete(name); },
        document: { getElementById: id => id === 'brand-logo' ? logo : favicon,
            createElement: tag => { assert.equal(tag, 'script'); return {}; },
            head: { appendChild(node) { script = node; assets.push(node.src); if (!holdPrism) node[prismError ? 'onerror' : 'onload'](); } } },
        load: async path => {
            loaded.push(new URL(path, "http://device/helio-bootstrap.js").pathname);
            if (productImportFails && path === './helio-app.js') throw new Error('product module unavailable');
            if (path === './helio-app.js') {
                if (productImportGate) await productImportGate;
                return { acceptInitialPresetIndexResponse(pending, lifecycle, module) {
                    handoffs.push(pending); lifecycles.push(lifecycle); productIdentities.push(module); return acceptIndex;
                } };
            }
        },
        fetch: async (path, options) => {
            if (path === '/api/presets') {
                indexRequests.push(path);
                responseSignal = options.signal;
                if (indexError) throw indexError;
                if (holdHeaders) return new Promise((_resolve, reject) => responseSignal.listeners.push(() => reject(abortError)));
                return index;
            }
            identity.push(path);
            assert.equal(path, '/api/modules/Firmware', 'UI selection reads only firmware identity, not the complete render tree');
            if (broken) throw new Error('offline');
            return {ok:true, json:async () => firmwareModule};
        } });
    vm.runInContext(source, context);
    return { context, loaded, assets, identity, logo, favicon, indexRequests, handoffs, lifecycles, productIdentities, firmwareModule, index, timers, listeners, abortError,
        expire() { for (const timer of [...timers.values()]) { assert.equal(timer.delay, 7000); timer.callback(); } },
        pagehide() { listeners.get('pagehide')?.(); },
        aborted: () => aborted, cancelled: () => cancelled, jsonReads: () => jsonReads, script: () => script };
}
async function boot(pathname, firmware, broken = false) {
    const fixture = bootFixture(pathname, firmware, { broken });
    await fixture.context.start();
    return fixture.loaded;
}
test('HELIO root starts only the custom product dashboard', async () => {
    assert.deepEqual(await boot('/', 'helio1'), ['/helio-app.js']);
});
test('NEBULITE root starts the product dashboard independently of generic MoonLight', async () => {
    assert.deepEqual(await boot('/', 'nebulite-s3'), ['/helio-app.js']);
});
test('generic editor remains directly reachable on product firmware', async () => {
    assert.deepEqual(await boot('/modules', 'helio1'), ['/app.js']);
});
test('generic firmware and unavailable identity retain the diagnostic interface', async () => {
    assert.deepEqual(await boot('/', 'esp32'), ['/app.js']);
    assert.deepEqual(await boot('/', '', true), ['/app.js']);
});

test('HELIO and NEBULITE bootstrap avoid generic logo and Prism requests', async () => {
    for (const firmware of ['helio1', 'nebulite-s3']) {
        const fixture = bootFixture('/', firmware);
        await fixture.context.start();
        assert.deepEqual(fixture.assets, []);
        assert.equal(fixture.logo.src, undefined);
        assert.equal(fixture.logo.hidden, true);
        assert.equal(fixture.favicon.href, undefined);
        assert.deepEqual(fixture.loaded, ['/helio-app.js']);
        assert.deepEqual(fixture.indexRequests, ['/api/presets']);
        assert.equal(fixture.handoffs.length, 1);
    }
    const html = readFileSync(new URL('../../src/ui/index.html', import.meta.url), 'utf8');
    assert.doesNotMatch(html, /<(?:img|script|link)\b[^>]*(?:src|href)=["'][^"']*(?:moonmodules-logo\.png|prism\.js)/i);
});

test('generic direct, fallback and product-import failure restore real logo and Prism before the diagnostic app', async () => {
    for (const [pathname, firmware, options] of [
        ['/modules', 'helio1', {}], ['/', 'esp32', {}], ['/', '', { broken: true }],
        ['/', 'helio1', { productImportFails: true }],
    ]) {
        const fixture = bootFixture(pathname, firmware, options);
        await fixture.context.start();
        assert.deepEqual(fixture.assets, ['/prism.js']);
        assert.equal(fixture.logo.src, '/moonmodules-logo.png');
        assert.equal(fixture.logo.hidden, false);
        assert.equal(fixture.favicon.href, '/moonmodules-logo.png');
        assert.equal(fixture.favicon.type, 'image/png');
        assert.equal(fixture.loaded.at(-1), '/app.js');
        if (pathname === '/modules') assert.deepEqual(fixture.identity, []);
        assert.equal(fixture.indexRequests.length, options.productImportFails ? 1 : 0);
    }
});

test('Prism remains a prerequisite on the generic route and its load failure still allows diagnostics', async () => {
    const fixture = bootFixture('/modules', 'helio1', { holdPrism: true });
    const starting = fixture.context.start();
    assert.deepEqual(fixture.loaded, []);
    fixture.script().onerror();
    await starting;
    assert.deepEqual(fixture.loaded, ['/app.js']);
    assert.deepEqual(fixture.assets, ['/prism.js']);
});

test('a verified product fetches one unconsumed index while the product module import is still pending', async () => {
    let release;
    const gate = new Promise(resolve => { release = resolve; });
    const fixture = bootFixture('/', 'helio1', { productImportGate: gate });
    const starting = fixture.context.start();
    for (let i = 0; i < 10 && !fixture.indexRequests.length; ++i) await Promise.resolve();
    assert.deepEqual(fixture.indexRequests, ['/api/presets']);
    assert.deepEqual(fixture.loaded, ['/helio-app.js']);
    assert.equal(fixture.handoffs.length, 0);
    assert.equal(fixture.jsonReads(), 0);
    release();
    await starting;
    assert.equal(fixture.handoffs.length, 1);
    assert.equal(await fixture.handoffs[0], fixture.index);
    assert.equal(fixture.jsonReads(), 0, 'only the canonical preset client may consume the response JSON');
    assert.equal(fixture.aborted(), 0);
});

test('failed product imports and declined index handoffs abort and release the unused response', async () => {
    for (const options of [{ productImportFails: true }, { acceptIndex: false }]) {
        const fixture = bootFixture('/', 'helio1', options);
        await fixture.context.start();
        for (let i = 0; i < 10 && !fixture.cancelled(); ++i) await Promise.resolve();
        assert.equal(fixture.indexRequests.length, 1);
        assert.equal(fixture.aborted(), 1);
        assert.equal(fixture.cancelled(), 1);
        assert.equal(fixture.jsonReads(), 0);
    }
});

test('a rejected initial index does not switch a verified product to the generic UI or rewrite its error', async () => {
    const error = new Error('index temporarily offline');
    const fixture = bootFixture('/', 'nebulite-s3', { indexError: error });
    await fixture.context.start();
    assert.deepEqual(fixture.loaded, ['/helio-app.js']);
    assert.deepEqual(fixture.assets, []);
    assert.equal(fixture.handoffs.length, 1);
    await assert.rejects(fixture.handoffs[0], received => received === error);
    assert.equal(fixture.lifecycles[0].discarded, false);
    assert.equal(fixture.timers.size, 0);
    assert.equal(fixture.listeners.size, 0);
});

test('the initial index deadline aborts stalled headers and preserves the original fetch failure', async () => {
    const fixture = bootFixture('/', 'helio1', { holdHeaders: true });
    await fixture.context.start();
    fixture.expire();
    await assert.rejects(fixture.handoffs[0], error => error === fixture.abortError);
    assert.equal(fixture.aborted(), 1);
    assert.equal(fixture.timers.size, 0);
    assert.equal(fixture.listeners.size, 0);
});

test('the initial index deadline covers a stalled body until canonical JSON processing finishes', async () => {
    const fixture = bootFixture('/', 'helio1', { holdBody: true });
    await fixture.context.start();
    const client = createPresetClient({ initialIndexResponse: fixture.handoffs[0], initialIndexLifecycle: fixture.lifecycles[0],
        fetchFn() { assert.fail('the first list cannot start a second GET'); } });
    const loading = client.list();
    for (let i = 0; i < 10 && !fixture.jsonReads(); ++i) await Promise.resolve();
    assert.equal(fixture.jsonReads(), 1);
    assert.equal(fixture.timers.size, 1);
    fixture.expire();
    await assert.rejects(loading, error => error.code === 'invalid-response');
    assert.equal(fixture.aborted(), 1);
    assert.equal(fixture.timers.size, 0);
    assert.equal(fixture.listeners.size, 0);
});

test('pagehide during canonical initial body consumption fails once and an explicit later list reads fresh data', async () => {
    const fixture = bootFixture('/', 'helio1', { holdBody: true });
    await fixture.context.start();
    let fresh = 0;
    const client = createPresetClient({ initialIndexResponse: fixture.handoffs[0], initialIndexLifecycle: fixture.lifecycles[0],
        fetchFn: async url => {
            assert.equal(url, '/api/presets'); ++fresh;
            return { ok: true, status: 200, headers: { get: () => null },
                json: async () => ({ items: ['explicit recovery'] }) };
        } });
    const loading = client.list();
    for (let i = 0; i < 10 && !fixture.jsonReads(); ++i) await Promise.resolve();
    assert.equal(fixture.jsonReads(), 1);
    fixture.pagehide();
    await assert.rejects(loading, error => error.code === 'invalid-response');
    assert.equal(fresh, 0, 'a consumed body abort cannot silently replay the index');
    assert.equal(fixture.aborted(), 1);
    assert.equal(fixture.timers.size, 0); assert.equal(fixture.listeners.size, 0);
    assert.deepEqual(await client.list(), { items: ['explicit recovery'] });
    assert.equal(fresh, 1); assert.equal(fixture.jsonReads(), 1);
});

test('an initial index that expires before product import completes is skipped by the first canonical list', async () => {
    let release;
    const gate = new Promise(resolve => { release = resolve; });
    const fixture = bootFixture('/', 'helio1', { holdHeaders: true, productImportGate: gate });
    const starting = fixture.context.start();
    for (let i = 0; i < 10 && !fixture.indexRequests.length; ++i) await Promise.resolve();
    fixture.expire();
    release();
    await starting;
    assert.equal(fixture.lifecycles[0].discarded, true);
    let fresh = 0;
    const client = createPresetClient({ initialIndexResponse: fixture.handoffs[0], initialIndexLifecycle: fixture.lifecycles[0],
        fetchFn: async () => { ++fresh; return { ok: true, status: 200, headers: { get: () => null },
            json: async () => ({ items: ['fresh after slow import'] }) }; } });
    assert.deepEqual(await client.list(), { items: ['fresh after slow import'] });
    assert.equal(fresh, 1);
    assert.equal(fixture.jsonReads(), 0);
    assert.equal(fixture.timers.size, 0);
});

test('an already buffered index that expires while waiting for the first state cannot poison the first mount', async () => {
    const fixture = bootFixture('/', 'helio1');
    await fixture.context.start();
    fixture.expire(); // the product is imported, but its first state and preset mount are still pending
    assert.equal(fixture.lifecycles[0].discarded, true);
    let fresh = 0;
    const client = createPresetClient({ initialIndexResponse: fixture.handoffs[0], initialIndexLifecycle: fixture.lifecycles[0],
        fetchFn: async () => { ++fresh; return { ok: true, status: 200, headers: { get: () => null },
            json: async () => ({ items: [`fresh ${fresh}`] }) }; } });
    assert.deepEqual(await client.list(), { items: ['fresh 1'] });
    assert.deepEqual(await client.list(), { items: ['fresh 2'] });
    assert.equal(fixture.jsonReads(), 0);
    assert.equal(fixture.cancelled(), 1);
    assert.equal(fixture.aborted(), 1);
    assert.equal(fixture.timers.size, 0);
    assert.equal(fixture.listeners.size, 0);
});

test('successful canonical index consumption releases its timer and unloading hook without aborting', async () => {
    const fixture = bootFixture('/', 'helio1');
    await fixture.context.start();
    const client = createPresetClient({ initialIndexResponse: fixture.handoffs[0], initialIndexLifecycle: fixture.lifecycles[0],
        fetchFn() { assert.fail('the first list cannot start a second GET'); } });
    assert.deepEqual(await client.list(), { items: [] });
    assert.equal(fixture.jsonReads(), 1);
    assert.equal(fixture.timers.size, 0);
    assert.equal(fixture.listeners.size, 0);
    fixture.pagehide();
    assert.equal(fixture.aborted(), 0);
});

test('unloading cancels an unconsumed initial fetch and legacy browsers use the regular list path', async () => {
    const fixture = bootFixture('/', 'helio1', { holdHeaders: true });
    await fixture.context.start();
    fixture.pagehide();
    await assert.rejects(fixture.handoffs[0], error => error === fixture.abortError);
    assert.equal(fixture.aborted(), 1);
    assert.equal(fixture.timers.size, 0);
    assert.equal(fixture.listeners.size, 0);
    const legacy = bootFixture('/', 'helio1', { abortSupport: false });
    await legacy.context.start();
    assert.deepEqual(legacy.loaded, ['/helio-app.js']);
    assert.deepEqual(legacy.indexRequests, []);
    assert.deepEqual(legacy.handoffs, []);
    assert.equal(legacy.timers.size, 0);
});

test('a page restored after pagehide before its first mount reads a fresh index instead of the cancelled transfer', async () => {
    const fixture = bootFixture('/', 'helio1', { holdHeaders: true });
    await fixture.context.start();
    fixture.pagehide();
    assert.equal(fixture.lifecycles[0].discarded, true);
    let fresh = 0;
    // A bfcache pageshow reconnect can deliver the first values snapshot and mount only now.
    const client = createPresetClient({ initialIndexResponse: fixture.handoffs[0], initialIndexLifecycle: fixture.lifecycles[0],
        fetchFn: async () => { ++fresh; return { ok: true, status: 200, headers: { get: () => null },
            json: async () => ({ items: ['fresh after pageshow'] }) }; } });
    assert.deepEqual(await client.list(), { items: ['fresh after pageshow'] });
    assert.equal(fresh, 1);
    assert.equal(fixture.jsonReads(), 0);
    assert.equal(fixture.aborted(), 1);
    assert.equal(fixture.timers.size, 0);
    assert.equal(fixture.listeners.size, 0);
});


test('product handoff carries only the exact verified Firmware module alongside the one raw index response', async () => {
    for (const firmware of ['helio1', 'nebulite-s3']) {
        const fixture = bootFixture('/', firmware);
        await fixture.context.start();
        assert.equal(fixture.productIdentities.length, 1);
        assert.equal(fixture.productIdentities[0], fixture.firmwareModule);
        assert.equal(fixture.handoffs.length, 1);
        assert.equal(fixture.jsonReads(), 0);
        assert.deepEqual(fixture.identity, ['/api/modules/Firmware']);
        assert.deepEqual(fixture.indexRequests, ['/api/presets']);
        assert.equal(Object.hasOwn(fixture.productIdentities[0], 'modules'), false, 'identity is not a fabricated runtime tree');
    }
    for (const [path, firmware] of [['/modules', 'helio1'], ['/', 'esp32']]) {
        const fixture = bootFixture(path, firmware);
        await fixture.context.start();
        assert.deepEqual(fixture.productIdentities, []);
        assert.deepEqual(fixture.indexRequests, []);
    }
});
