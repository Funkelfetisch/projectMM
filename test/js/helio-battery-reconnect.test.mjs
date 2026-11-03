import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

test('HELIO state connection follows the page protocol and preserves the port', () => {
    const source = readFileSync(new URL('../../src/ui/helio-app.js', import.meta.url), 'utf8');
    const start = source.indexOf('function connectWs() {');
    const end = source.indexOf('\nfunction setWsDot(', start);
    assert.ok(start >= 0 && end > start);
    for (const [protocol, expected] of [['https:', 'wss://example.test:8443/ws'], ['http:', 'ws://example.test:8443/ws']]) {
        let url;
        class Socket { constructor(value) { url = value; } }
        const context = vm.createContext({ WebSocket: Socket, location: { host: 'example.test:8443', protocol },
            ws: null, wsReconnectTimer: null, helioBatteryConnected: false,
            syncHelioBatteryStatus() {}, clearTimeout() {} });
        vm.runInContext(source.slice(start, end), context);
        context.connectWs();
        assert.equal(url, expected);
    }
});

test('battery receipt ages while its dashboard is unmounted', async () => {
    const app = readFileSync(new URL('../../src/ui/helio-app.js', import.meta.url), 'utf8');
    const battery = readFileSync(new URL('../../src/ui/helio-battery.js', import.meta.url), 'utf8');
    const { batteryReceiptTracker, batteryView } = await import(`data:text/javascript;base64,${Buffer.from(battery).toString('base64')}`);
    const start = app.indexOf('function syncHelioBatteryStatus(');
    const end = app.indexOf('\n// A half-open socket', start);
    assert.ok(start >= 0 && end > start);
    const sample = { id: 1, revision: 3, sample: { presence: 'present', state: 'fresh', voltageMv: 4000,
        percent: 50, ageMs: 100, estimate: 'voltage' } };
    const state = { modules: [{ type: 'BatteryService', children: [{ type: 'BatteryChannel', controls: [
        { name: 'slot', value: 1 }, { name: 'sample', value: JSON.stringify(sample) },
    ] }] }] };
    let now = 0, rendered;
    const host = { dataset: {}, replaceChildren() {}, hidden: false };
    const context = vm.createContext({ state, batteryView, helioBatteryConnected: true,
        helioBatteryReceipts: batteryReceiptTracker(), performance: { now: () => now },
        document: { querySelector: () => null }, renderBatteryStatus: (_document, view) => { rendered = view; return {}; } });
    vm.runInContext(app.slice(start, end), context);
    context.syncHelioBatteryStatus(); // Snapshot arrives while the dashboard is absent.
    now = 12001;
    context.syncHelioBatteryStatus(host);
    assert.equal(rendered.packs[0].percent, null);
    assert.equal(rendered.packs[0].reason, 'telemetry-timeout');
});

test('replacing a socket hides the old battery reading until a new snapshot', () => {
    const source = readFileSync(new URL('../../src/ui/helio-app.js', import.meta.url), 'utf8');
    const start = source.indexOf('function connectWs() {');
    const end = source.indexOf('\nfunction setWsDot(', start);
    assert.ok(start >= 0 && end > start);
    class Socket {
        static instances = [];
        constructor() { Socket.instances.push(this); }
        close() {} // The old close callback can arrive after replacement.
    }
    let visible;
    const context = vm.createContext({ WebSocket: Socket, location: { host: 'example.test' },
        ws: null, wsReconnectTimer: null, wsPaused: false, helioBatteryConnected: true,
        syncHelioBatteryStatus() { visible = context.helioBatteryConnected; },
        clearTimeout() {}, clearInterval() {} });
    vm.runInContext(source.slice(start, end), context);
    context.connectWs();
    const old = Socket.instances[0];
    assert.equal(visible, false);
    context.helioBatteryConnected = true; // A current snapshot has arrived.
    context.connectWs();
    assert.equal(visible, false);
    old.onclose(); // This stale callback must not affect the replacement.
    assert.equal(context.helioBatteryConnected, false);
});
