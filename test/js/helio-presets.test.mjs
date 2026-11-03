import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const source = readFileSync(new URL('../../src/ui/helio-presets.js', import.meta.url), 'utf8');
const { createPresetClient, PresetApiError, PresetEditor } = await import(
    `data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);

const path = ['Effects', 'Actual Layer', 'My Twinkles'];
const document = () => ({
    Effects: { 'Actual Layer': { 'My Twinkles': { speed: 10, 'option 1': false, enabled: true } } },
    $origin: { scene: 'colortwinkles', catalog: 'catalog-2' },
    $uiSchema: { effectType: 'HelioColorTwinklesEffect', effectPath: path,
        controls: [{ name: 'speed', type: 'uint8', min: 0, max: 255, default: 10 }] },
});
const effect = doc => doc.Effects['Actual Layer']['My Twinkles'];

function response(body, { status = 200, etag = null } = {}) {
    return { ok: status >= 200 && status < 300, status,
        headers: { get: name => name.toLowerCase() === 'etag' ? etag : null },
        json: async () => body };
}

function adapter(responses) {
    const requests = [];
    const client = createPresetClient({ fetchFn: async (url, options = {}) => {
        requests.push({ url, options });
        assert.ok(responses.length, 'every request has an explicit fixture response');
        return responses.shift();
    } });
    return { client, requests };
}

test('preset list and raw document retain backend identity, roles and native metadata', async () => {
    const list = { items: [{ name: 'Orange & blue', slot: 2, etag: 'e1', roles: ['effects'], kind: 'look', originScene: 'colortwinkles' }],
        active: { effects: 'Orange & blue' }, limit: 64 };
    const fixture = adapter([response(list), response(document(), { etag: '"e1"' })]);
    assert.deepEqual(await fixture.client.list(), list);
    assert.deepEqual(await fixture.client.read('Orange & blue'), { document: document(), etag: '"e1"' });
    assert.deepEqual(fixture.requests.map(request => request.url), ['/api/presets', '/api/presets/Orange%20%26%20blue']);
});

test('an empty preset list carries a nullable string active-look hint', async () => {
    const listing = { items: [], active: { effects: null }, limit: 64 };
    const fixture = adapter([response(listing)]);
    assert.deepEqual(await fixture.client.list(), listing);
});

test('every native preset GET retries at most two explicit 503 replies after consuming their bodies', async () => {
    const list = { items: [], active: { effects: null }, limit: 64 };
    const catalog = { catalog: 'catalog-2', items: [{ id: 'orange-blue', name: 'Orange & blue' }] };
    const cases = [
        { call: client => client.list(), url: '/api/presets', result: list, expected: list },
        { call: client => client.read('Orange & blue'), url: '/api/presets/Orange%20%26%20blue',
            result: document(), expected: { document: document(), etag: '"latest"' } },
        { call: client => client.listTemplates(), url: '/api/preset-templates', result: catalog, expected: catalog },
        { call: client => client.readTemplate('orange-blue'), url: '/api/preset-templates/orange-blue',
            result: document(), expected: { document: document(), etag: '"latest"' } },
    ];
    for (const item of cases) {
        const events = [];
        let attempt = 0;
        const client = createPresetClient({ fetchFn: async url => {
            const current = attempt++;
            assert.equal(url, item.url);
            events.push(`fetch${current}`);
            const res = response(current < 2 ? { error: 'temporary pressure' } : item.result,
                { status: current < 2 ? 503 : 200, etag: current < 2 ? `"obsolete-${current}"` : '"latest"' });
            const read = res.json;
            res.json = async () => { events.push(`json${current}`); return read(); };
            return res;
        }, waitFn: async delay => { events.push(`wait${delay}`); } });
        assert.deepEqual(await item.call(client), item.expected);
        assert.deepEqual(events, ['fetch0', 'json0', 'wait150', 'fetch1', 'json1', 'wait300', 'fetch2', 'json2']);
        assert.equal(attempt, 3);
    }
});

test('GET retry exhaustion preserves the final 503 and also consumes malformed 503 error bodies', async () => {
    const waits = [], reads = [];
    let attempts = 0;
    const final = { error: 'still no send room', code: 'busy-final' };
    const client = createPresetClient({ fetchFn: async () => {
        const current = attempts++;
        const res = response(current === 2 ? final : null, { status: 503 });
        res.json = async () => {
            reads.push(current);
            if (current < 2) throw new SyntaxError('upstream HTML error page');
            return final;
        };
        return res;
    }, waitFn: async delay => { waits.push(delay); } });
    await assert.rejects(client.list(), error => error instanceof PresetApiError &&
        error.status === 503 && error.code === 'busy-final' && error.message === final.error && error.details === final);
    assert.equal(attempts, 3);
    assert.deepEqual(reads, [0, 1, 2]);
    assert.deepEqual(waits, [150, 300]);
});

test('an initial index response is consumed once through the normal client and later lists fetch fresh data', async () => {
    const initial = { items: [{ name: 'Earlier snapshot' }] }, fresh = { items: [{ name: 'Fresh snapshot' }] };
    let reads = 0, requests = 0;
    const prepared = response(initial);
    prepared.json = async () => { ++reads; return initial; };
    const client = createPresetClient({ initialIndexResponse: Promise.resolve(prepared),
        fetchFn: async url => { ++requests; assert.equal(url, '/api/presets'); return response(fresh); } });
    assert.deepEqual(await client.list(), initial);
    assert.equal(requests, 0);
    assert.equal(reads, 1);
    assert.deepEqual(await client.list(), fresh);
    assert.equal(requests, 1);
    assert.equal(reads, 1);
});

test('concurrent list calls atomically consume the initial response before awaiting it', async () => {
    const gate = deferred();
    let requests = 0;
    const client = createPresetClient({ initialIndexResponse: gate.promise,
        fetchFn: async () => { ++requests; return response({ items: ['fresh'] }); } });
    const first = client.list();
    const second = client.list();
    assert.deepEqual(await second, { items: ['fresh'] });
    gate.resolve(response({ items: ['initial'] }));
    assert.deepEqual(await first, { items: ['initial'] });
    assert.equal(requests, 1);
});

test('initial index 404, network and invalid JSON errors remain genuine errors and cannot poison later lists', async () => {
    for (const kind of ['404', 'network', 'json']) {
        const original = new Error('original network failure');
        const prepared = response({ error: 'original missing index' }, { status: kind === '404' ? 404 : 200 });
        if (kind === 'json') prepared.json = async () => { throw new SyntaxError('truncated JSON'); };
        const pending = kind === 'network' ? Promise.reject(original) : Promise.resolve(prepared);
        let requests = 0, finished = 0;
        const client = createPresetClient({ initialIndexResponse: pending, initialIndexLifecycle: { finish() { ++finished; } },
            fetchFn: async () => { ++requests; return response({ items: ['recovered'] }); } });
        await assert.rejects(client.list(), error => kind === 'network'
            ? error.code === 'network' && error.details === original
            : kind === '404' ? error.status === 404 && error.message === 'original missing index'
                : error.code === 'invalid-response');
        assert.equal(requests, 0, 'these failures cannot silently retry or fabricate an empty index');
        assert.equal(finished, 1);
        assert.deepEqual(await client.list(), { items: ['recovered'] });
        assert.equal(requests, 1);
        assert.equal(finished, 1);
    }
});

test('an initial 503 uses the existing retry budget and consumes its error body before waiting', async () => {
    const events = [];
    const prepared = response({ error: 'initial pressure' }, { status: 503 });
    prepared.json = async () => { events.push('initial-json'); return { error: 'initial pressure' }; };
    let requests = 0;
    const client = createPresetClient({ initialIndexResponse: Promise.resolve(prepared),
        waitFn: async delay => { events.push(`wait:${delay}`); }, fetchFn: async () => {
            ++requests; events.push(`fetch:${requests}`);
            return requests === 1 ? response({ error: 'more pressure' }, { status: 503 }) : response({ items: ['ready'] });
        } });
    assert.deepEqual(await client.list(), { items: ['ready'] });
    assert.deepEqual(events, ['initial-json', 'wait:150', 'fetch:1', 'wait:300', 'fetch:2']);
    assert.equal(requests, 2);
});

test('writes, deletes and applies discard an unused initial index rather than showing its stale membership or active hint', async () => {
    for (const [method, field, mutate] of [
        ['PUT', 'stored', client => client.write('New', document(), { create: true })],
        ['DELETE', 'deleted', client => client.remove('Old', { etag: 'old' })],
        ['POST', 'applied', client => client.apply('Current', { etag: 'current' })],
    ]) {
        let cancelled = 0, reads = 0;
        const prepared = response({ items: ['stale'] });
        prepared.body = { async cancel() { ++cancelled; } };
        prepared.json = async () => { assert.fail('the obsolete response body must not be consumed'); };
        const client = createPresetClient({ initialIndexResponse: Promise.resolve(prepared), fetchFn: async (_url, options = {}) => {
            if (options.method) { assert.equal(options.method, method); return response({ [field]: true, etag: 'new' }); }
            ++reads; return response({ items: ['fresh'] });
        } });
        await mutate(client);
        assert.equal(cancelled, 1);
        assert.deepEqual(await client.list(), { items: ['fresh'] });
        assert.equal(reads, 1);
    }
});

test('native preset PUT POST and DELETE are never retried on 503', async () => {
    for (const [method, call] of [
        ['PUT', client => client.write('Orange & blue', document(), { create: true })],
        ['POST', client => client.apply('Orange & blue', { etag: 'e1' })],
        ['DELETE', client => client.remove('Orange & blue', { etag: 'e1' })],
    ]) {
        const calls = [], waits = [];
        let consumed = 0;
        const client = createPresetClient({ fetchFn: async (url, options) => {
            calls.push({ url, options });
            const res = response({ error: 'mutation refused under pressure' }, { status: 503 });
            res.json = async () => { ++consumed; return { error: 'mutation refused under pressure' }; };
            return res;
        }, waitFn: async delay => { waits.push(delay); } });
        await assert.rejects(call(client), error => error.status === 503);
        assert.equal(calls.length, 1);
        assert.equal(calls[0].options.method, method);
        assert.equal(consumed, 1);
        assert.deepEqual(waits, []);
    }
});

test('network and non-503 HTTP failures bypass GET retries', async () => {
    for (const status of [400, 404, 412, 429, 500, 507]) {
        let attempts = 0;
        const waits = [];
        const client = createPresetClient({ fetchFn: async () => {
            ++attempts; return response({ error: `failure ${status}` }, { status });
        }, waitFn: async delay => { waits.push(delay); } });
        await assert.rejects(client.read('Orange & blue'), error => error.status === status && error.message === `failure ${status}`);
        assert.equal(attempts, 1);
        assert.deepEqual(waits, []);
    }
    let attempts = 0;
    const waits = [];
    const client = createPresetClient({ fetchFn: async () => { ++attempts; throw new Error('offline'); },
        waitFn: async delay => { waits.push(delay); } });
    await assert.rejects(client.list(), error => error.code === 'network' && error.message === 'offline');
    assert.equal(attempts, 1);
    assert.deepEqual(waits, []);
});

test('a successful GET with invalid JSON shape or missing ETag is not retried', async () => {
    for (const kind of ['json', 'shape', 'etag']) {
        let attempts = 0;
        const waits = [];
        const client = createPresetClient({ fetchFn: async () => {
            ++attempts;
            const res = response(kind === 'shape' ? [] : document());
            if (kind === 'json') res.json = async () => { throw new SyntaxError('truncated'); };
            return res;
        }, waitFn: async delay => { waits.push(delay); } });
        await assert.rejects(client.read('Orange & blue'), error =>
            error.code === (kind === 'etag' ? 'missing-etag' : 'invalid-response'));
        assert.equal(attempts, 1);
        assert.deepEqual(waits, []);
    }
});

test('a truncated HTTP 200 index is rejected without discarding mounted preset records or drafts', async () => {
    const listing = { items: [{ name: 'Orange & blue', kind: 'look', etag: 'e1' }],
        active: { effects: null }, limit: 64 };
    const truncated = response(null);
    truncated.json = async () => JSON.parse('{"items":[{"name":"Orange & blue"}');
    const fixture = adapter([response(listing), truncated]);
    const element = () => ({ isConnected: true, children: [],
        setAttribute() {}, append(...children) { this.children.push(...children); },
        appendChild(child) { this.children.push(child); },
        replaceChildren(...children) { this.children = children; } });
    const cardsSource = readFileSync(new URL('../../src/ui/helio-preset-cards.js', import.meta.url), 'utf8')
        .replace(/^import[^\n]+\n/gm, '')
        .replace('export class HelioPresetCards', 'class HelioPresetCards');
    const Cards = vm.runInNewContext(cardsSource + '\nHelioPresetCards;', {
        document: { createElement: element }, PresetEditor,
    });
    const cards = new Cards({ client: fixture.client });
    cards.addForm = element;
    cards.card = element;
    await cards.mount(element());
    const record = cards.records.get('Orange & blue');
    assert.ok(record);
    let disposed = false;
    const editor = { dirty: true, dispose() { disposed = true; }, document: document() };
    record.editor = editor;
    cards.selected = record.name;

    await cards.mount(element());

    assert.equal(cards.status.textContent, 'Invalid preset response');
    assert.equal(cards.records.size, 1);
    assert.equal(cards.records.get(record.name), record);
    assert.equal(record.editor, editor);
    assert.equal(cards.selected, record.name);
    assert.equal(disposed, false);
    assert.equal(fixture.requests.length, 2);
});

test('the controller ASCII-name validation failure remains HTTP 400 in the client', async () => {
    const fixture = adapter([response({ error: 'invalid preset name' }, { status: 400 })]);
    await assert.rejects(fixture.client.read('Warm weiß'), error =>
        error.status === 400 && error.message === 'invalid preset name');
    assert.equal(fixture.requests[0].url, '/api/presets/Warm%20wei%C3%9F');
});

test('create and overwrite send native documents with distinct exact HTTP preconditions', async () => {
    const fixture = adapter([
        response({ stored: true, name: 'Sun', etag: 'e1', created: true, applied: false }),
        response({ stored: true, name: 'Sun', etag: 'e2', created: false, applied: false }),
    ]);
    const draft = document();
    const created = await fixture.client.write('Sun', draft, { create: true });
    const overwritten = await fixture.client.write('Sun', draft, { etag: created.etag });
    assert.equal(created.etag, '"e1"');
    assert.equal(overwritten.etag, '"e2"');
    assert.equal(overwritten.applied, false);
    assert.deepEqual(fixture.requests[0], { url: '/api/presets/Sun', options: {
        method: 'PUT', headers: { 'Content-Type': 'application/json', 'If-None-Match': '*' }, body: JSON.stringify(draft),
    } });
    assert.deepEqual(fixture.requests[1].options.headers, { 'Content-Type': 'application/json', 'If-Match': '"e1"' });
    assert.deepEqual(JSON.parse(fixture.requests[1].options.body), draft);
});

test('delete is conditional and apply is an explicit conditional look-only operation', async () => {
    const fixture = adapter([
        response({ deleted: true, name: 'Warm light', etag: 'e1' }),
        response({ applied: true, name: 'Warm light', etag: 'e2', changes: 4 }),
    ]);
    await fixture.client.remove('Warm light', { etag: 'e1' });
    await fixture.client.apply('Warm light', { etag: '"e2"' });
    assert.deepEqual(fixture.requests[0], { url: '/api/presets/Warm%20light', options: {
        method: 'DELETE', headers: { 'If-Match': '"e1"' },
    } });
    assert.deepEqual(fixture.requests[1], { url: '/api/presets/Warm%20light/apply', options: {
        method: 'POST', headers: { 'Content-Type': 'application/json', 'If-Match': '"e2"' }, body: '{"scope":"look"}',
    } });
});

test('templates use their backend catalog and preserve schema, ranges and real effect paths', async () => {
    const catalog = { catalog: 'catalog-2', items: [{ id: 'colortwinkles', name: 'Twinkles' }] };
    const fixture = adapter([response(catalog), response(document())]);
    assert.deepEqual(await fixture.client.listTemplates(), catalog);
    assert.deepEqual(await fixture.client.readTemplate('colortwinkles'), { document: document(), etag: null });
    assert.deepEqual(fixture.requests.map(request => request.url), ['/api/preset-templates', '/api/preset-templates/colortwinkles']);
});

test('live tuning optionally guards the actual output while an explicit play has no output precondition', async () => {
    const outputEtag = 'a'.repeat(64);
    const nextOutputEtag = 'b'.repeat(64);
    const fixture = adapter([response({ applied: true, name: 'Sun', etag: 'e1', outputEtag: nextOutputEtag }),
        response({ error: 'output changed elsewhere' }, { status: 412 })]);
    const applied = await fixture.client.apply('Sun', { etag: 'e1', outputEtag });
    assert.equal(fixture.requests[0].options.headers['If-Output-Match'], `"${outputEtag}"`);
    assert.equal(applied.outputEtag, nextOutputEtag);
    assert.equal(fixture.requests[0].options.body, '{"scope":"look"}');
    await assert.rejects(fixture.client.apply('Sun', { etag: 'e1', outputEtag }), error =>
        error.status === 412 && error.message === 'output changed elsewhere');
});

for (const status of [412, 507]) {
    test(`HTTP ${status} exposes the API error and status to the card`, async () => {
        const fixture = adapter([response({ error: `backend detail ${status}` }, { status })]);
        await assert.rejects(fixture.client.write('Sun', document(), { etag: 'e1' }), error => {
            assert.ok(error instanceof PresetApiError);
            assert.equal(error.status, status);
            assert.equal(error.message, `backend detail ${status}`);
            assert.equal(error.code, status === 412 ? 'conflict' : 'storage-full');
            return true;
        });
    });
}

test('network failure and missing ETag are visible errors and never trigger an unconditional write', async () => {
    const offline = createPresetClient({ fetchFn: async () => { throw new Error('offline'); } });
    await assert.rejects(offline.list(), error => error.code === 'network' && error.message === 'offline');
    const fixture = adapter([response(document())]);
    await assert.rejects(fixture.client.read('Sun'), error => error.code === 'missing-etag');
    await assert.rejects(fixture.client.write('Sun', document()), /ETag is required/);
    assert.throws(() => fixture.client.remove('Sun'), /ETag is required/);
    assert.throws(() => fixture.client.apply('Sun'), /ETag is required/);
    assert.equal(fixture.requests.length, 1);
});

function deferred() {
    let resolve, reject;
    const promise = new Promise((done, fail) => { resolve = done; reject = fail; });
    return { promise, resolve, reject };
}

function timers() {
    const callbacks = new Map();
    let next = 0;
    return { callbacks,
        setTimeoutFn: (callback, delay) => { const id = ++next; callbacks.set(id, { callback, delay }); return id; },
        clearTimeoutFn: id => callbacks.delete(id),
        run() { const pending = [...callbacks.values()]; callbacks.clear(); pending.forEach(item => item.callback()); },
    };
}

async function until(predicate) {
    for (let attempt = 0; attempt < 30 && !predicate(); attempt++) await Promise.resolve();
    assert.ok(predicate(), 'the save checkpoint was reached');
}

function playbackFixture({ onSelect, onApply, document: domDocument } = {}) {
    const cardsSource = readFileSync(new URL('../../src/ui/helio-preset-cards.js', import.meta.url), 'utf8')
        .replace(/^import[^\n]+\n/gm, '')
        .replace('export class HelioPresetCards', 'class HelioPresetCards');
    const Cards = vm.runInNewContext(cardsSource + '\nHelioPresetCards;', { PresetEditor, document: domDocument });
    const requests = [];
    const cards = new Cards({ client: { apply(name, options) {
        const gate = deferred();
        requests.push({ name, options, ...gate });
        return gate.promise;
    } }, onSelect, onApply });
    cards.status = { textContent: '' };
    const records = Object.fromEntries(['A', 'B', 'C'].map(name => [name, {
        name, editor: { etag: `etag-${name}`, dirty: false, document: document(), async flush() {} },
    }]));
    for (const record of Object.values(records)) cards.records.set(record.name, record);
    cards.ensureEditor = async record => record.editor;
    return { cards, records, requests };
}

test('preset selection is immediate and confirmed apply completes while status observation is still pending', async () => {
    const observation = deferred();
    const selections = [];
    let fixture;
    fixture = playbackFixture({ onSelect(record) {
        selections.push(record.name);
        assert.equal(fixture.cards.selected, record.name);
        assert.equal(fixture.cards.pending.record, record);
    }, onApply: () => observation.promise });
    const draft = JSON.stringify(fixture.records.A.editor.document);
    const playing = fixture.cards.play(fixture.records.A);
    assert.deepEqual(selections, ['A']);
    await until(() => fixture.requests.length === 1);
    fixture.requests[0].resolve({ outputEtag: 'a'.repeat(64) });
    await until(() => fixture.cards.applying === false);
    await playing;
    assert.equal(fixture.cards.selected, 'A');
    assert.equal(fixture.records.A.outputEtag, 'a'.repeat(64));
    assert.equal(JSON.stringify(fixture.records.A.editor.document), draft);
    observation.resolve();
});

test('synchronous and asynchronous post-apply observation failures retain confirmed selection and output token', async () => {
    for (const asynchronous of [false, true]) {
        const fixture = playbackFixture({ onApply() {
            const error = new Error('readback unavailable');
            if (asynchronous) return Promise.reject(error);
            throw error;
        } });
        const playing = fixture.cards.play(fixture.records.A);
        await until(() => fixture.requests.length === 1);
        fixture.requests[0].resolve({ outputEtag: 'a'.repeat(64) });
        await playing;
        await until(() => fixture.cards.status.textContent.includes('readback unavailable'));
        assert.equal(fixture.cards.selected, 'A');
        assert.equal(fixture.records.A.outputEtag, 'a'.repeat(64));
        assert.equal(fixture.cards.status.textContent, 'Preset applied. Live status unavailable: readback unavailable');
    }
});

test('selection observation errors do not prevent applying or erase a subsequently confirmed token', async () => {
    for (const asynchronous of [false, true]) {
        const observer = deferred();
        const fixture = playbackFixture({ onSelect() {
            if (asynchronous) return observer.promise;
            throw new Error('selection observation unavailable');
        } });
        const playing = fixture.cards.play(fixture.records.A);
        await until(() => fixture.requests.length === 1);
        fixture.requests[0].resolve({ outputEtag: 'a'.repeat(64) });
        await playing;
        if (asynchronous) {
            observer.reject(new Error('selection observation unavailable'));
            await until(() => fixture.cards.status.textContent.includes('observation unavailable'));
        }
        assert.equal(fixture.cards.selected, 'A');
        assert.equal(fixture.records.A.outputEtag, 'a'.repeat(64));
    }
});

test('a newer preset plays while an older observation waits and its late failure cannot replace the newer status', async () => {
    const oldObservation = deferred();
    const observed = [];
    const fixture = playbackFixture({ onApply(record) {
        observed.push(record.name);
        return record.name === 'A' ? oldObservation.promise : undefined;
    } });
    const a = fixture.cards.play(fixture.records.A);
    await until(() => fixture.requests.length === 1);
    fixture.requests[0].resolve({ outputEtag: 'a'.repeat(64) });
    await until(() => fixture.cards.applying === false);
    await a;
    const b = fixture.cards.play(fixture.records.B);
    await until(() => fixture.requests.length === 2);
    assert.equal(fixture.requests[1].name, 'B');
    fixture.requests[1].resolve({ outputEtag: 'b'.repeat(64) });
    await b;
    oldObservation.reject(new Error('obsolete A failure'));
    await Promise.resolve();
    await Promise.resolve();
    assert.deepEqual(observed, ['A', 'B']);
    assert.equal(fixture.cards.selected, 'B');
    assert.equal(fixture.cards.status.textContent, 'B');
    assert.equal(fixture.records.B.outputEtag, 'b'.repeat(64));
});

test('rapid native preset A B C intent skips B after the sent A and observes only the latest confirmed selection', async () => {
    const observed = [];
    const fixture = playbackFixture({ onApply: record => observed.push(record.name) });
    const playing = fixture.cards.play(fixture.records.A);
    await until(() => fixture.requests.length === 1);
    await fixture.cards.play(fixture.records.B);
    await fixture.cards.play(fixture.records.C);
    assert.equal(fixture.cards.selected, 'C');
    fixture.requests[0].resolve({ outputEtag: 'a'.repeat(64) });
    await until(() => fixture.requests.length === 2);
    assert.deepEqual(fixture.requests.map(request => request.name), ['A', 'C']);
    assert.deepEqual(observed, []);
    fixture.requests[1].resolve({ outputEtag: 'c'.repeat(64) });
    await playing;
    assert.deepEqual(observed, ['C']);
    assert.equal(fixture.cards.selected, 'C');
    assert.equal(fixture.records.C.outputEtag, 'c'.repeat(64));
});

test('explicit controls and Play hydrate immediately during a background preview cooldown', async () => {
    const fixture = playbackFixture();
    delete fixture.cards.ensureEditor; // exercise the real foreground read queue and editor lifecycle
    const reads = [];
    fixture.cards.client.read = async name => { reads.push(name); return { document: document(), etag: `etag-${name}` }; };
    for (const name of ['A', 'B']) {
        fixture.records[name].editor = null;
        fixture.records[name].previewRetryAt = 999999;
    }
    const editor = await fixture.cards.ensureEditor(fixture.records.A);
    assert.ok(editor instanceof PresetEditor);
    assert.deepEqual(reads, ['A']);
    assert.equal(fixture.requests.length, 0, 'opening controls reads a file without applying it');

    const playing = fixture.cards.play(fixture.records.B);
    await until(() => fixture.requests.length === 1);
    assert.deepEqual(reads, ['A', 'B']);
    assert.equal(fixture.requests[0].name, 'B');
    fixture.requests[0].resolve({ outputEtag: 'b'.repeat(64) });
    await playing;
    assert.equal(fixture.cards.selected, 'B');
    assert.equal(fixture.records.B.outputEtag, 'b'.repeat(64));
    fixture.records.A.editor.dispose();
    fixture.records.B.editor.dispose();
});

function editorFixture(write = async () => ({ etag: 'e2' }), options = {}) {
    const clock = timers();
    const writes = [], changes = [], errors = [];
    const initial = document();
    const editor = new PresetEditor({ name: 'My card', document: initial, etag: 'e1', debounceMs: 350,
        client: { write: (...args) => { writes.push(args); return write(...args); },
            apply: () => assert.fail('saving never applies a preset') },
        ...clock, onChange: snapshot => changes.push(snapshot), onError: error => errors.push(error), ...options });
    return { editor, clock, writes, changes, errors, initial };
}

test('default browser timers keep their Window receiver during edit cancellation and disposal', () => {
    const context = vm.createContext({ initial: document(), path });
    vm.runInContext(`
        globalThis.timerCalls = [];
        globalThis.setTimeout = function(callback, delay) {
            if (this !== globalThis) throw new TypeError('Illegal invocation: setTimeout');
            timerCalls.push(['set', delay]);
            return timerCalls.length;
        };
        globalThis.clearTimeout = function(timer) {
            if (this !== globalThis) throw new TypeError('Illegal invocation: clearTimeout');
            timerCalls.push(['clear', timer]);
        };
        ${source.replace(/^export /gm, '')}
        const editor = new PresetEditor({name:'Window timer',document:initial,etag:'e1',
            client:{write(){throw new Error('A timer fixture never saves');}}});
        editor.edit(path,'speed',20);
        editor.edit(path,'speed',30);
        editor.dispose();
    `, context);
    assert.deepEqual(JSON.parse(JSON.stringify(context.timerCalls)), [
        ['set', 400], ['clear', 1], ['set', 400], ['clear', 3],
    ]);
});

test('successive slider and boolean edits debounce into one full-document save without autoplay', async () => {
    const fixture = editorFixture();
    fixture.editor.edit(path, 'speed', 20);
    fixture.editor.edit(path, 'speed', 30);
    fixture.editor.edit(path, 'option 1', true);
    assert.equal(fixture.writes.length, 0);
    assert.equal(fixture.clock.callbacks.size, 1);
    assert.equal([...fixture.clock.callbacks.values()][0].delay, 350);
    fixture.clock.run();
    await until(() => fixture.editor.status === 'saved');
    assert.equal(fixture.writes.length, 1);
    assert.equal(fixture.writes[0][0], 'My card');
    assert.deepEqual(effect(fixture.writes[0][1]), { speed: 30, 'option 1': true, enabled: true });
    assert.deepEqual(fixture.writes[0][1].$uiSchema, fixture.initial.$uiSchema);
    assert.equal(effect(fixture.initial).speed, 10);
    assert.equal(fixture.editor.dirty, false);
    assert.ok(fixture.changes.some(snapshot => snapshot.status === 'saved'));
});

test('in-flight ACK preserves a newer draft and the next serialized save uses its fresh ETag', async () => {
    const first = deferred(), second = deferred();
    let calls = 0;
    const fixture = editorFixture(() => (++calls === 1 ? first.promise : second.promise));
    fixture.editor.edit(path, 'speed', 20);
    const saving = fixture.editor.flush();
    fixture.editor.edit(path, 'speed', 90);
    fixture.clock.run();
    assert.equal(fixture.writes.length, 1, 'the next request waits for the old ACK');
    first.resolve({ etag: 'e2', document: fixture.initial });
    await until(() => fixture.writes.length === 2);
    assert.equal(effect(fixture.editor.document).speed, 90);
    assert.equal(effect(fixture.writes[0][1]).speed, 20);
    assert.equal(effect(fixture.writes[1][1]).speed, 90);
    assert.deepEqual(fixture.writes[1][2], { etag: '"e2"', create: false });
    second.resolve({ etag: 'e3' });
    await saving;
    assert.equal(fixture.editor.etag, '"e3"');
    assert.equal(fixture.editor.dirty, false);
    const detached = fixture.editor.document;
    effect(detached).speed = 255;
    assert.equal(effect(fixture.editor.document).speed, 90);
});

test('replace imports a private full document and remains serialized behind an older save', async () => {
    const first = deferred();
    let calls = 0;
    const fixture = editorFixture(() => ++calls === 1 ? first.promise : Promise.resolve({ etag: 'e3' }));
    fixture.editor.edit(path, 'speed', 20);
    const saving = fixture.editor.flush();
    const replacement = document();
    effect(replacement).speed = 180;
    effect(replacement)['option 1'] = true;
    replacement.$origin.scene = 'fresh-template';
    fixture.editor.replace(replacement);
    effect(replacement).speed = 1;
    fixture.editor.flush();
    first.resolve({ etag: 'e2' });
    await saving;
    assert.equal(fixture.writes.length, 2);
    assert.equal(effect(fixture.writes[1][1]).speed, 180);
    assert.equal(fixture.writes[1][1].$origin.scene, 'fresh-template');
    assert.equal(fixture.editor.name, 'My card');
    assert.equal(fixture.editor.etag, '"e3"');
});

test('a stale ACK from a disposed card cannot update another card or call its observers', async () => {
    const first = deferred();
    const old = editorFixture(() => first.promise);
    old.editor.edit(path, 'speed', 50);
    const saving = old.editor.flush();
    old.editor.dispose();
    const count = old.changes.length;
    const current = editorFixture(async () => ({ etag: 'new-card' }), { name: 'Other card' });
    current.editor.edit(path, 'speed', 210);
    first.resolve({ etag: 'old-ack' });
    await saving;
    assert.equal(old.changes.length, count);
    assert.equal(old.editor.status, 'disposed');
    assert.equal(current.editor.name, 'Other card');
    assert.equal(effect(current.editor.document).speed, 210);
    assert.equal(current.editor.etag, '"e1"');
});

test('412 preserves the latest draft and stops automatic retries, including edits after the conflict', async () => {
    const first = deferred();
    const fixture = editorFixture(() => first.promise);
    fixture.editor.edit(path, 'speed', 20);
    const saving = fixture.editor.flush();
    fixture.editor.edit(path, 'speed', 90);
    const error = new PresetApiError('Changed elsewhere', { status: 412, code: 'conflict' });
    first.reject(error);
    await assert.rejects(saving, error);
    assert.equal(fixture.editor.status, 'conflict');
    assert.equal(effect(fixture.editor.document).speed, 90);
    assert.equal(fixture.editor.dirty, true);
    assert.equal(fixture.clock.callbacks.size, 0);
    fixture.editor.edit(path, 'speed', 100);
    fixture.clock.run();
    await assert.rejects(fixture.editor.flush(), error);
    assert.equal(effect(fixture.editor.document).speed, 100);
    assert.equal(fixture.writes.length, 1);
    assert.equal(fixture.editor.etag, '"e1"');
    assert.deepEqual(fixture.errors, [error]);
});

test('storage-full errors remain visible with a dirty draft and no implicit apply or overwrite', async () => {
    const error = new PresetApiError('No free slot', { status: 507, code: 'storage-full' });
    const fixture = editorFixture(async () => { throw error; });
    fixture.editor.edit(path, 'speed', 77);
    await assert.rejects(fixture.editor.flush(), error);
    assert.equal(fixture.editor.status, 'error');
    assert.equal(fixture.editor.error.status, 507);
    assert.equal(fixture.editor.dirty, true);
    assert.equal(effect(fixture.editor.document).speed, 77);
    assert.equal(fixture.clock.callbacks.size, 0);
    assert.deepEqual(fixture.errors, [error]);
});

test('new editor saves are create-only and an invalid path cannot mutate the draft', async () => {
    const fixture = editorFixture(async () => ({ etag: 'created' }), { etag: null });
    assert.throws(() => fixture.editor.edit(['Effects', 'Missing'], 'speed', 30), /Unknown preset path/);
    assert.equal(effect(fixture.editor.document).speed, 10);
    assert.equal(fixture.clock.callbacks.size, 0);
    fixture.editor.edit(path, 'option 1', true);
    await fixture.editor.flush();
    assert.deepEqual(fixture.writes[0][2], { etag: null, create: true });
    assert.equal(fixture.editor.etag, '"created"');
});

test('an unsaved template is created by explicit flush without requiring an artificial control change', async () => {
    const fixture = editorFixture(async () => ({ etag: 'created' }), { etag: null });
    assert.equal(fixture.editor.dirty, true);
    assert.equal(fixture.writes.length, 0);
    assert.equal(fixture.clock.callbacks.size, 0);
    await fixture.editor.flush();
    assert.equal(fixture.writes.length, 1);
    assert.deepEqual(fixture.writes[0][1], fixture.initial);
    assert.deepEqual(fixture.writes[0][2], { etag: null, create: true });
    assert.equal(fixture.editor.dirty, false);
});

function ringDocuments() {
    const saved = document();
    saved.$slot = 9;
    saved.$origin = { scene: 'ripple', catalog: 'old-catalog', seedSource: 'customer' };
    saved.$uiSchema.effectType = 'RingsEffect';
    saved.$uiSchema.controls[0].label = 'My speed';
    saved.$customer = { keep: true };
    Object.assign(effect(saved), { type: 'RingsEffect', speed: 1, count: 7, thickness: 5, hue_shift: 13 });
    saved.Drivers = { palette: 6 };
    const templatePath = ['Effects', 'Current Layer', 'BLE Scene'];
    const template = {
        Effects: { 'Current Layer': { 'BLE Scene': { type: 'RingsEffect', speed: 42, 'background brightness': 0 } } },
        Drivers: { palette: 0 },
        $origin: { scene: 'ripple', catalog: 'new-catalog' },
        $uiSchema: { effectType: 'RingsEffect', effectPath: templatePath, controls: [
            { name: 'speed', type: 'uint8', min: 1, max: 255, default: 42 },
            { name: 'background brightness', type: 'uint8', min: 0, max: 255, default: 0, path: templatePath },
            { name: 'palette', type: 'palette', default: 0, path: ['Drivers'] },
            { name: 'pin', type: 'uint8', default: 35, path: ['Drivers', 'Strip'] },
        ] },
    };
    return { saved, template };
}

function schemaEditor(doc, options = {}) {
    return new PresetEditor({ client: { async write() { assert.fail('schema hydration must not write'); } },
        name: 'My renamed rings', document: doc, etag: 'old-etag', setTimeoutFn: () => 1, clearTimeoutFn() {}, ...options });
}

test('newer controls preserve saved values and only the next explicit edit conditionally saves additions', async () => {
    const { saved, template } = ringDocuments();
    effect(saved)['background brightness'] = 17;
    const before = JSON.parse(JSON.stringify(saved));
    const writes = [], timers = [], notifications = [];
    const editor = schemaEditor(saved, { client: { async write(name, document, options) {
        writes.push({ name, document, options }); return { etag: 'new-etag' };
    } }, setTimeoutFn: (callback, delay) => { timers.push({ callback, delay }); return 1; },
        onChange: state => notifications.push(state) });
    assert.equal(editor.hydrateSchema(template), true);
    const next = editor.document;
    assert.deepEqual(next.Effects, before.Effects);
    assert.deepEqual(next.Drivers, before.Drivers);
    assert.deepEqual(next.$origin, before.$origin);
    assert.deepEqual(next.$customer, before.$customer);
    assert.equal(next.$slot, 9);
    assert.deepEqual(next.$uiSchema.effectPath, path);
    assert.deepEqual(next.$uiSchema.controls[0], before.$uiSchema.controls[0]);
    assert.deepEqual(next.$uiSchema.controls.map(control => control.name), ['speed', 'background brightness', 'palette']);
    assert.deepEqual(next.$uiSchema.controls[1].path, path);
    assert.deepEqual(next.$uiSchema.controls[2].path, ['Drivers']);
    assert.equal(editor.etag, '"old-etag"');
    assert.equal(editor.dirty, false);
    assert.equal(editor.status, 'idle');
    assert.deepEqual(writes, []);
    assert.deepEqual(timers, []);
    assert.deepEqual(notifications, []);
    assert.equal(editor.hydrateSchema(template), false);
    editor.edit(path, 'background brightness', 32);
    assert.equal(timers.length, 1);
    await editor.flush();
    assert.equal(writes.length, 1);
    assert.deepEqual(writes[0].options, { etag: '"old-etag"', create: false });
    assert.equal(effect(writes[0].document)['background brightness'], 32);
    assert.equal(effect(writes[0].document).speed, 1);
    assert.equal(writes[0].document.$origin.catalog, 'old-catalog');
    assert.equal(writes[0].document.Drivers.palette, 6);
    assert.equal(writes[0].document.$slot, 9);
    assert.equal(editor.dirty, false);
});

test('new schema defaults use the saved effect path without transplanting a current controller layer', () => {
    const { saved, template } = ringDocuments();
    delete saved.Drivers;
    const editor = schemaEditor(saved);
    assert.equal(editor.hydrateSchema(template), true);
    assert.equal(effect(editor.document)['background brightness'], 0);
    assert.deepEqual(Object.keys(editor.document.Effects), ['Actual Layer']);
    assert.deepEqual(Object.keys(editor.document.Effects['Actual Layer']), ['My Twinkles']);
    assert.deepEqual(editor.document.Drivers, { palette: 0 });
    assert.equal(editor.document.Drivers.Strip, undefined);
    assert.equal(editor.dirty, false);
});

test('incompatible template identity or missing target leaves an existing dirty draft unchanged', () => {
    for (const kind of ['type', 'origin', 'target', 'source-type']) {
        const { saved, template } = ringDocuments();
        const editor = schemaEditor(saved);
        editor.edit(path, 'speed', 3);
        const before = editor.snapshot();
        if (kind === 'type') template.$uiSchema.effectType = 'SolidEffect';
        if (kind === 'origin') template.$origin.scene = 'solid';
        if (kind === 'target') template.$uiSchema.effectPath = ['Effects', 'Missing'];
        if (kind === 'source-type') template.Effects['Current Layer']['BLE Scene'].type = 'SolidEffect';
        assert.equal(editor.hydrateSchema(template), false);
        assert.deepEqual(editor.snapshot(), before);
    }
});

test('template adjuncts share the background slot, prioritize explicit Controls and cache by scene and type', async () => {
    const fixture = playbackFixture();
    delete fixture.cards.ensureEditor;
    const { saved, template } = ringDocuments();
    fixture.records.A.editor = schemaEditor(saved);
    const gates = { B: deferred(), C: deferred(), D: deferred(), template: deferred() };
    for (const name of ['B', 'C', 'D']) {
        const record = fixture.records[name] ||= { name };
        record.editor = null;
        fixture.cards.records.set(name, record);
    }
    const events = [];
    fixture.cards.client.read = name => {
        events.push(name); assert.ok(fixture.cards.reading <= 2); assert.ok(fixture.cards.backgroundReading <= 1);
        return gates[name].promise;
    };
    fixture.cards.client.readTemplate = scene => {
        events.push(`template:${scene}`); assert.ok(fixture.cards.reading <= 2);
        assert.ok(fixture.cards.backgroundReading <= 1); return gates.template.promise;
    };
    const background = ['B', 'C'].map(name => fixture.cards.ensureEditor(fixture.records[name], { background: true }));
    const primary = fixture.cards.ensureEditor(fixture.records.D);
    const controls = fixture.cards.ensureControls(fixture.records.A);
    await until(() => fixture.cards.readQueue.length === 2);
    assert.deepEqual(Array.from(fixture.cards.readQueue, request => request.record.name), ['A', 'C']);
    assert.deepEqual(events, ['B', 'D']);
    gates.B.resolve({ document: document(), etag: 'b' });
    await until(() => events.length === 3);
    assert.deepEqual(events, ['B', 'D', 'template:ripple']);
    gates.C.resolve({ document: document(), etag: 'c' });
    gates.D.resolve({ document: document(), etag: 'd' });
    gates.template.resolve({ document: template });
    await Promise.all([...background, primary, controls]);
    assert.equal(fixture.cards.reading, 0);
    assert.equal(fixture.cards.backgroundReading, 0);
    assert.equal(effect(fixture.records.A.editor.document)['background brightness'], 0);
    await fixture.cards.ensureControls(fixture.records.A);
    assert.equal(events.filter(event => event.startsWith('template:')).length, 1);
    const second = { name: 'Another copy', editor: schemaEditor(saved) };
    fixture.cards.records.set(second.name, second);
    await fixture.cards.ensureControls(second);
    assert.equal(events.filter(event => event.startsWith('template:')).length, 1);
    assert.equal(effect(second.editor.document)['background brightness'], 0);
});

test('failed or obsolete template fetches retain the draft and can be retried without writing', async () => {
    const fixture = playbackFixture();
    const { saved, template } = ringDocuments();
    const record = fixture.records.A;
    record.editor = schemaEditor(saved);
    record.status = { textContent: '' };
    let attempts = 0;
    fixture.cards.client.readTemplate = async () => {
        if (++attempts === 1) throw new Error('HTTP 503');
        return { document: template };
    };
    record.editor.edit(path, 'speed', 4);
    const before = record.editor.snapshot();
    assert.equal(await fixture.cards.ensureControls(record), record.editor);
    assert.deepEqual(record.editor.snapshot(), before);
    assert.match(record.status.textContent, /settings are preserved/);
    await fixture.cards.ensureControls(record);
    assert.equal(attempts, 2);
    assert.equal(effect(record.editor.document).speed, 4);
    assert.equal(effect(record.editor.document)['background brightness'], 0);
    assert.equal(record.editor.dirty, true);
    const gate = deferred();
    fixture.cards.templateSchemas.clear();
    fixture.cards.client.readTemplate = () => gate.promise;
    const previous = record.editor;
    const opening = fixture.cards.ensureControls(record);
    await until(() => fixture.cards.reading === 1);
    record.editor = schemaEditor(saved);
    const replacement = record.editor.snapshot();
    gate.resolve({ document: template });
    await opening;
    assert.deepEqual(record.editor.snapshot(), replacement);
    assert.notEqual(record.editor, previous);
});

test('preview hydration and explicit Play never fetch newer template schemas', async () => {
    const fixture = playbackFixture();
    delete fixture.cards.ensureEditor;
    const { saved } = ringDocuments();
    const record = fixture.records.A;
    record.editor = null;
    let reads = 0, applies = 0;
    fixture.cards.client.read = async () => { ++reads; return { document: saved, etag: 'old' }; };
    fixture.cards.client.readTemplate = () => assert.fail('only opening Controls may hydrate schema');
    fixture.cards.client.apply = async () => { ++applies; return { applied: true, outputEtag: 'output' }; };
    await fixture.cards.ensureEditor(record, { background: true });
    await fixture.cards.play(record);
    assert.equal(reads, 1);
    assert.equal(applies, 1);
    assert.equal(effect(record.editor.document)['background brightness'], undefined);
    assert.equal(record.editor.dirty, false);
});

test('a delayed template extends the current draft rather than replacing edits made while it loaded', async () => {
    const fixture = playbackFixture();
    const { saved, template } = ringDocuments();
    const record = fixture.records.A;
    record.editor = schemaEditor(saved);
    const gate = deferred();
    fixture.cards.client.readTemplate = () => gate.promise;
    const opening = fixture.cards.ensureControls(record);
    await until(() => fixture.cards.reading === 1);
    record.editor.edit(path, 'speed', 5);
    gate.resolve({ document: template });
    await opening;
    assert.equal(effect(record.editor.document).speed, 5);
    assert.equal(effect(record.editor.document)['background brightness'], 0);
    assert.equal(record.editor.dirty, true);
    assert.equal(record.editor.etag, '"old-etag"');
});

test('an obsolete template type is discarded from the schema cache and never replaces the saved scene', async () => {
    const fixture = playbackFixture();
    const { saved, template } = ringDocuments();
    const record = fixture.records.A;
    record.editor = schemaEditor(saved);
    const before = record.editor.snapshot();
    let attempts = 0;
    fixture.cards.client.readTemplate = async () => {
        const doc = JSON.parse(JSON.stringify(template));
        if (++attempts === 1) doc.$uiSchema.effectType = 'SolidEffect';
        return { document: doc };
    };
    await fixture.cards.ensureControls(record);
    assert.deepEqual(record.editor.snapshot(), before);
    assert.equal(fixture.cards.templateSchemas.size, 0);
    await fixture.cards.ensureControls(record);
    assert.equal(attempts, 2);
    assert.equal(effect(record.editor.document)['background brightness'], 0);
});

function presetDom() {
    const node = tagName => ({ tagName, dataset: {}, children: [], listeners: {}, isConnected: true,
        classList: { toggle() {}, remove() {} }, setAttribute() {},
        append(...children) { this.children.push(...children); },
        appendChild(child) { this.children.push(child); },
        replaceChildren(...children) { this.children = children; },
        contains(target) { return this === target || this.children.some(child => child.contains?.(target)); },
        addEventListener(name, callback) { this.listeners[name] = callback; } });
    return { activeElement: null, createElement: node };
}

test('opening saved Controls paints immediately and late additive controls do not replace a focused input', async () => {
    const dom = presetDom();
    const fixture = playbackFixture({ document: dom });
    const { saved, template } = ringDocuments();
    const record = fixture.records.A;
    record.editor = schemaEditor(saved);
    const gate = deferred();
    fixture.cards.client.readTemplate = () => gate.promise;
    let paints = 0, currentInput;
    fixture.cards.makeControls = () => {
        ++paints;
        const panel = dom.createElement('section');
        currentInput = dom.createElement('input');
        panel.appendChild(currentInput);
        return panel;
    };
    const card = fixture.cards.card(record);
    const details = card.children.find(child => child.tagName === 'details');
    paints = 0;
    details.open = true;
    details.listeners.toggle();
    assert.equal(paints, 1, 'the existing saved schema renders before the template request can complete');
    assert.equal(record.controlsOpen, true);
    await until(() => fixture.cards.reading === 1);
    const focused = currentInput;
    dom.activeElement = focused;
    record.editor.edit(path, 'speed', 7);
    gate.resolve({ document: template });
    await until(() => record.content.listeners.focusout);
    assert.equal(paints, 1);
    assert.equal(record.content.contains(focused), true);
    assert.equal(effect(record.editor.document).speed, 7);
    assert.equal(effect(record.editor.document)['background brightness'], 0);
    dom.activeElement = null;
    record.content.listeners.focusout();
    await until(() => paints === 2);
    assert.equal(record.content.contains(focused), false);
    assert.equal(effect(record.editor.document).speed, 7);
    assert.equal(record.editor.dirty, true);
});

test('a closed or remounted Controls view is not repainted by its obsolete hydration callback', async () => {
    const dom = presetDom();
    const fixture = playbackFixture({ document: dom });
    const { saved, template } = ringDocuments();
    const record = fixture.records.A;
    record.editor = schemaEditor(saved);
    const gate = deferred();
    fixture.cards.client.readTemplate = () => gate.promise;
    let paints = 0;
    fixture.cards.makeControls = () => { ++paints; return dom.createElement('section'); };
    const first = fixture.cards.card(record);
    const details = first.children.find(child => child.tagName === 'details');
    details.open = true;
    details.listeners.toggle();
    await until(() => fixture.cards.reading === 1);
    details.open = false;
    details.listeners.toggle();
    fixture.cards.card(record);
    const before = paints;
    gate.resolve({ document: template });
    await until(() => fixture.cards.reading === 0);
    await Promise.resolve();
    assert.equal(paints, before);
    assert.equal(record.controlsOpen, false);
    assert.equal(record.editor.dirty, false);
});

test('a newer Play skips an obsolete queued file read without cancelling active reads or applying the old card', async () => {
    const fixture = playbackFixture();
    delete fixture.cards.ensureEditor;
    const gates = { X: deferred(), Y: deferred(), B: deferred() };
    const reads = [], applied = [];
    fixture.cards.client.read = name => { reads.push(name); return gates[name].promise; };
    fixture.cards.client.apply = async name => { applied.push(name); return { outputEtag: 'confirmed' }; };
    for (const name of ['X', 'Y']) fixture.cards.records.set(name, { name, editor: null });
    for (const name of ['A', 'B']) fixture.records[name].editor = null;
    const active = ['X', 'Y'].map(name => fixture.cards.ensureEditor(fixture.cards.records.get(name)));
    const playing = fixture.cards.play(fixture.records.A);
    await until(() => fixture.records.A.queuedRead);
    await fixture.cards.play(fixture.records.B);
    await until(() => fixture.records.B.queuedRead);
    assert.deepEqual(reads, ['X', 'Y']);
    assert.deepEqual(Array.from(fixture.cards.readQueue, request => request.record.name), ['B']);
    gates.X.resolve({ document: document(), etag: 'x' });
    await until(() => reads.includes('B'));
    assert.equal(reads.includes('A'), false);
    gates.B.resolve({ document: document(), etag: 'b' });
    gates.Y.resolve({ document: document(), etag: 'y' });
    await Promise.all([...active, playing]);
    assert.deepEqual(applied, ['B']);
    assert.equal(fixture.cards.selected, 'B');
    assert.equal(fixture.cards.reading, 0);
});

test('cold Controls file reads use a reserved slot while only one preview read runs', async () => {
    const fixture = playbackFixture();
    delete fixture.cards.ensureEditor;
    const gates = { A: deferred(), B: deferred(), C: deferred() };
    const reads = [];
    for (const record of Object.values(fixture.records)) record.editor = null;
    fixture.cards.client.read = name => { reads.push(name); return gates[name].promise; };
    const previews = ['A', 'B'].map(name => fixture.cards.ensureEditor(fixture.records[name], { background: true }));
    await until(() => reads.length === 1);
    assert.deepEqual(reads, ['A']);
    assert.equal(fixture.cards.reading, 1);
    assert.equal(fixture.cards.backgroundReading, 1);
    const controls = fixture.cards.ensureEditor(fixture.records.C);
    await until(() => reads.length === 2);
    assert.deepEqual(reads, ['A', 'C']);
    assert.equal(fixture.cards.reading, 2);
    assert.equal(fixture.cards.backgroundReading, 1);
    gates.C.resolve({ document: document(), etag: 'c' });
    await controls;
    assert.deepEqual(reads, ['A', 'C'], 'a second background read cannot take the reserved slot');
    gates.A.resolve({ document: document(), etag: 'a' });
    await until(() => reads.includes('B'));
    gates.B.resolve({ document: document(), etag: 'b' });
    await Promise.all(previews);
    assert.equal(fixture.cards.reading, 0);
    assert.equal(fixture.cards.backgroundReading, 0);
});

test('an already active preview file is promoted without restarting it or exceeding two total reads', async () => {
    const fixture = playbackFixture();
    delete fixture.cards.ensureEditor;
    const gates = { A: deferred(), B: deferred() };
    const reads = [];
    fixture.records.A.editor = fixture.records.B.editor = null;
    fixture.cards.client.read = name => { reads.push(name); return gates[name].promise; };
    const a = fixture.cards.ensureEditor(fixture.records.A, { background: true });
    const b = fixture.cards.ensureEditor(fixture.records.B, { background: true });
    await until(() => reads.length === 1);
    const primary = fixture.cards.ensureEditor(fixture.records.A);
    await until(() => reads.length === 2);
    assert.deepEqual(reads, ['A', 'B']);
    assert.equal(fixture.cards.reading, 2);
    assert.equal(fixture.cards.backgroundReading, 1);
    gates.A.resolve({ document: document(), etag: 'a' });
    gates.B.resolve({ document: document(), etag: 'b' });
    await Promise.all([a, b, primary]);
    assert.equal(fixture.cards.reading, 0);
    assert.equal(fixture.cards.backgroundReading, 0);
});

test('template and palette metadata callbacks share one adjunct slot and leave room for a primary file', async () => {
    const fixture = playbackFixture();
    delete fixture.cards.ensureEditor;
    const gates = { schema: deferred(), palette: deferred(), file: deferred() };
    const events = [];
    const schema = fixture.cards.readDocument(fixture.records.A, false, () => { events.push('schema'); return gates.schema.promise; });
    const palette = fixture.cards.readDocument(fixture.records.A, false, () => { events.push('palette'); return gates.palette.promise; });
    fixture.records.B.editor = null;
    fixture.cards.client.read = () => { events.push('file'); return gates.file.promise; };
    const primary = fixture.cards.ensureEditor(fixture.records.B);
    await until(() => events.length === 2);
    assert.deepEqual(events, ['schema', 'file']);
    assert.equal(fixture.cards.reading, 2);
    assert.equal(fixture.cards.backgroundReading, 1);
    gates.file.resolve({ document: document(), etag: 'b' });
    await primary;
    assert.deepEqual(events, ['schema', 'file']);
    gates.schema.resolve({ document: document() });
    await until(() => events.length === 3);
    assert.equal(fixture.cards.backgroundReading, 1);
    gates.palette.resolve(true);
    await Promise.all([schema, palette]);
    assert.equal(fixture.cards.reading, 0);
    assert.equal(fixture.cards.backgroundReading, 0);
});

test('opening Controls reserves the adjunct slot for its small template before palette metadata begins', async () => {
    const dom = presetDom();
    const fixture = playbackFixture({ document: dom });
    delete fixture.cards.ensureEditor;
    const { saved, template } = ringDocuments();
    const record = fixture.records.A;
    record.editor = schemaEditor(saved);
    fixture.records.B.editor = null;
    const gates = { template: deferred(), palette: deferred(), primary: deferred() };
    const events = [];
    fixture.cards.client.readTemplate = () => { events.push('template'); return gates.template.promise; };
    fixture.cards.client.read = () => { events.push('primary'); return gates.primary.promise; };
    fixture.cards.onControls = target => fixture.cards.readDocument(target, false, () => {
        events.push('palette'); return gates.palette.promise;
    });
    fixture.cards.makeControls = () => dom.createElement('section');
    const card = fixture.cards.card(record);
    const details = card.children.find(child => child.tagName === 'details');
    details.open = true;
    details.listeners.toggle();
    await until(() => fixture.cards.readQueue.length === 1);
    assert.deepEqual(events, ['template']);
    assert.equal(fixture.cards.backgroundReading, 1);
    const primary = fixture.cards.ensureEditor(fixture.records.B);
    await until(() => events.length === 2);
    assert.deepEqual(events, ['template', 'primary']);
    assert.equal(fixture.cards.reading, 2);
    gates.template.resolve({ document: template });
    await until(() => events.length === 3);
    assert.deepEqual(events, ['template', 'primary', 'palette']);
    assert.equal(fixture.cards.reading, 2);
    assert.equal(fixture.cards.backgroundReading, 1);
    gates.primary.resolve({ document: document(), etag: 'b' });
    gates.palette.resolve(true);
    await primary;
    await until(() => fixture.cards.reading === 0);
    assert.equal(effect(record.editor.document)['background brightness'], 0);
    assert.equal(record.editor.dirty, false);
});

test('first runtime metadata refresh preserves a focused early draft and its in-flight native CAS save', async () => {
    const dom = presetDom(), fixture = playbackFixture({ document: dom });
    const record = fixture.records.A, saving = deferred(), palette = deferred(), requests = [];
    const saved = document(); saved.$customer = { label: 'Keep this' }; saved.$slot = 23;
    const client = createPresetClient({ fetchFn: async (url, options) => {
        requests.push({ url, options }); return saving.promise;
    } });
    record.editor = new PresetEditor({ client, name: record.name, document: saved, etag: 'early-etag',
        setTimeoutFn: () => 1, clearTimeoutFn() {} });
    const editor = record.editor;
    fixture.cards.ensureControls = async () => false;
    let runtimeReady = false, earlyMetadataCalls = 0, metadataCalls = 0, paints = 0, input;
    fixture.cards.onControls = async () => {
        if (!runtimeReady) { ++earlyMetadataCalls; return false; }
        ++metadataCalls; await palette.promise; return true;
    };
    fixture.cards.makeControls = () => {
        ++paints;
        const panel = dom.createElement('section');
        input = dom.createElement('input'); input.value = String(effect(editor.document).speed);
        panel.appendChild(input); return panel;
    };
    const card = fixture.cards.card(record), details = card.children.find(node => node.tagName === 'details');
    details.open = true; details.listeners.toggle();
    await until(() => record.controlsOpen && paints > 0 && earlyMetadataCalls === 1);
    const focused = input, content = record.content, before = paints;
    focused.value = '77'; focused.selectionStart = focused.selectionEnd = 2;
    dom.activeElement = focused;
    editor.edit(path, 'speed', 77);
    const pendingSave = editor.flush();
    await until(() => requests.length === 1);
    assert.equal(requests[0].options.method, 'PUT');
    assert.equal(requests[0].options.headers['If-Match'], '"early-etag"');
    assert.equal(effect(JSON.parse(requests[0].options.body)).speed, 77);
    runtimeReady = true;
    fixture.cards.refreshOpenControls();
    await until(() => metadataCalls === 1);
    assert.equal(paints, before, 'refresh-only hydration cannot initially rebuild open controls');
    palette.resolve();
    await until(() => content.listeners.focusout);
    assert.equal(record.card, card); assert.equal(record.content, content); assert.equal(record.editor, editor);
    assert.equal(content.contains(focused), true); assert.equal(dom.activeElement, focused);
    assert.equal(focused.value, '77'); assert.equal(focused.selectionStart, 2); assert.equal(focused.selectionEnd, 2);
    assert.equal(effect(editor.document).speed, 77); assert.equal(editor.dirty, true);
    saving.resolve(response({ stored: true, etag: 'saved-etag' }, { etag: '"saved-etag"' }));
    await pendingSave;
    assert.equal(editor.etag, '"saved-etag"'); assert.equal(editor.dirty, false);
    assert.equal(requests.length, 1); assert.equal(editor.document.$slot, 23);
    assert.deepEqual(editor.document.$customer, { label: 'Keep this' });
    dom.activeElement = null; content.listeners.focusout();
    await until(() => paints === before + 1);
    assert.equal(effect(editor.document).speed, 77);
    assert.equal(requests.length, 1, 'runtime hydration cannot rewrite or reapply the saved file');
    editor.dispose();
});

test('runtime metadata refresh ignores closed and unloaded early card editors', async () => {
    const dom = presetDom(), fixture = playbackFixture({ document: dom });
    fixture.cards.makeControls = () => dom.createElement('section');
    let called = 0;
    fixture.cards.onControls = async () => { ++called; return true; };
    const loaded = fixture.cards.card(fixture.records.A);
    loaded.children.find(node => node.tagName === 'details').open = false;
    fixture.records.B.editor = null; fixture.records.B.controlsOpen = true;
    const unloaded = fixture.cards.card(fixture.records.B);
    unloaded.children.find(node => node.tagName === 'details').open = true;
    fixture.cards.refreshOpenControls();
    await Promise.resolve(); await Promise.resolve();
    assert.equal(called, 0);
});
