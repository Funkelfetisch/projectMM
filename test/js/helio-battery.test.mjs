import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const source = readFileSync(new URL('../../src/ui/helio-battery.js', import.meta.url), 'utf8');
const { batteryReceiptTracker, batteryView, batteryPresentation, renderBatteryStatus } = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);

const sample = overrides => ({ presence: 'present', state: 'fresh', voltageMv: 4100,
    percent: 50, ageMs: 1000, estimate: 'voltage', ...overrides });
function channel(slot, reading = sample(), options = {}) {
    return { type: 'BatteryChannel', enabled: true, controls: [
        { name: 'slot', value: slot },
        { name: 'sample', value: JSON.stringify({ id: slot, sample: reading, charging: 'unknown' }) },
    ], ...options };
}
const service = children => ({ type: 'BatteryService', enabled: true, children });
const one = reading => batteryView([service([channel(1, reading)])]).packs[0];

test('an open but silent connection cannot keep a web battery estimate fresh', () => {
    const battery = channel(1);
    const setRevision = revision => { battery.controls[1].value = JSON.stringify({ id: 1, sample: sample(), charging: 'unknown', revision }); };
    const modules = [service([battery])], receipts = batteryReceiptTracker(12000);
    const view = now => batteryView(modules, { receipts, now }).packs[0];
    setRevision(10);
    assert.equal(view(0).percent, 50);
    assert.equal(view(12000).percent, 50);
    assert.equal(view(12001).percent, null);
    assert.equal(view(12001).reason, 'telemetry-timeout');
    setRevision(11);
    assert.equal(view(13000).percent, 50);
    setRevision(0); // revision wrap is still a new receipt
    assert.equal(view(14000).percent, 50);
    assert.equal(view(27001).percent, null);
    const oldFirmware = channel(2);
    assert.equal(batteryView([service([oldFirmware])], { receipts, now: 30000 }).packs[0].percent, 50);
});

test('replacing the state snapshot does not make an old receipt fresh again', () => {
    const receipts = batteryReceiptTracker(12000);
    const snapshot = revision => {
        const pack = channel(1);
        pack.controls[1].value = JSON.stringify({ id: 1, sample: sample(), revision });
        return [service([pack])];
    };
    assert.equal(batteryView(snapshot(7), { receipts, now: 0 }).packs[0].percent, 50);
    const stale = batteryView(snapshot(7), { receipts, now: 12001 }).packs[0];
    assert.equal(stale.percent, null);
    assert.equal(stale.reason, 'telemetry-timeout');
    assert.equal(batteryView(snapshot(8), { receipts, now: 13000 }).packs[0].percent, 50);
});

test('legacy readings without a revision expire even when snapshots repeat', () => {
    const receipts = batteryReceiptTracker(12000);
    const snapshot = () => [service([channel(1)])];
    const read = now => batteryView(snapshot(), { receipts, now }).packs[0];
    assert.equal(read(100).percent, 50);
    assert.equal(read(12100).percent, 50);
    assert.equal(read(12101).reason, 'telemetry-timeout');
    assert.equal(read(20000).percent, null);
    const updated = channel(1);
    updated.controls[1].value = JSON.stringify({ id: 1, sample: sample(), revision: 1 });
    assert.equal(batteryView([service([updated])], { receipts, now: 20001 }).packs[0].percent, 50);
});

test('invalid messages and disappearing revisions cannot revive a stale receipt', () => {
    const receipts = batteryReceiptTracker(12000), pack = channel(1);
    const read = now => batteryView([service([pack])], { receipts, now }).packs[0];
    const set = wire => { pack.controls[1].value = JSON.stringify(wire); };
    set({ id: 1, sample: sample(), revision: 7 });
    assert.equal(read(0).percent, 50);
    set({ id: 2, sample: sample(), revision: 8 });
    assert.equal(read(12001).reason, 'invalid-data');
    set({ id: 1, sample: sample(), revision: 7 });
    assert.equal(read(12001).reason, 'telemetry-timeout');
    set({ id: 1, sample: sample() });
    assert.equal(read(12002).reason, 'telemetry-timeout');
    set({ id: 1, sample: sample(), revision: 8 });
    assert.equal(read(12003).percent, 50);
});

test('malformed revision is not mistaken for legacy firmware without a revision', () => {
    for (const revision of [null, -1, 1.5, '7', 0x100000000]) {
        const pack = channel(1), receipts = batteryReceiptTracker();
        const read = () => batteryView([service([pack])], { receipts, now: 0 }).packs[0];
        pack.controls[1].value = JSON.stringify({ id: 1, sample: sample(), revision });
        assert.equal(read().reason, 'invalid-data');
        assert.equal(read().percent, null);
        pack.controls[1].value = JSON.stringify({ id: 1, sample: sample() });
        assert.equal(read().percent, 50); // An actually absent field is legacy firmware.
    }
});

test('battery status is absent for mains profiles and disabled services or ancestors', () => {
    assert.equal(batteryView([]), null);
    assert.equal(batteryView(null), null);
    assert.equal(batteryView([service([])]), null);
    assert.equal(batteryView([{ ...service([channel(1)]), enabled: false }]), null);
    assert.equal(batteryView([{ enabled: false, children: [service([channel(1)])] }]), null);
    assert.equal(batteryView([service([channel(1, sample(), { enabled: false })])]), null);
});

test('battery selection uses one enabled nested service and direct channels only', () => {
    const modules = [{ children: [{ ...service([channel(2)]), enabled: false },
        service([channel(1), { children: [channel(2)] }])] }];
    assert.deepEqual(batteryView(modules).packs.map(pack => pack.id), [1]);
});

test('multiple enabled battery services are ambiguous rather than selecting an unrelated reading', () => {
    for (const modules of [
        [service([channel(1)]), service([channel(2)])],
        [service([]), service([channel(1)])],
        [service([service([channel(1)])])],
    ]) {
        assert.deepEqual(batteryView(modules), { packs: [], error: 'invalid-configuration' });
        assert.deepEqual(batteryView(modules, { connected: false }), { packs: [], error: 'invalid-configuration' });
    }
});

test('independent battery channels retain their own levels and never manufacture an aggregate', () => {
    const view = batteryView([service([channel(2, sample({ percent: 90 })), channel(1, sample({ percent: 10 }))])]);
    assert.deepEqual(view.packs.map(pack => [pack.id, pack.percent]), [[1, 10], [2, 90]]);
    assert.equal(view.packs.length, 2);
});

test('invalid service configuration masks old readings and displays an explicit error', () => {
    const broken = { ...service([channel(1), channel(2)]), controls: [{ name: 'configuration', value: 'invalid' }] };
    const view = batteryView([broken]);
    assert.equal(view.error, 'invalid-configuration');
    assert.deepEqual(view.packs.map(pack => pack.percent), [null, null]);
    assert.deepEqual(view.packs.map(pack => pack.voltageMv), [null, null]);
    assert.match(batteryPresentation(view.packs[0]).detail, /configuration error/);
    const noValidSlots = batteryView([{ ...service([channel(0)]), controls: broken.controls }]);
    assert.equal(noValidSlots.error, 'invalid-configuration');
    assert.deepEqual(noValidSlots.packs, []);
});

test('a disconnected controller cannot present cached charge as current', () => {
    const modules = [service([channel(1, sample({ percent: 75 })), channel(2)])];
    const disconnected = batteryView(modules, { connected: false });
    for (const pack of disconnected.packs) {
        assert.equal(pack.percent, null);
        assert.equal(pack.voltageMv, null);
        assert.equal(batteryPresentation(pack).detail, 'Controller disconnected');
    }
    assert.equal(batteryView(modules, { connected: true }).packs[0].percent, 75);
    assert.equal(batteryView([], { connected: false }), null);
});

test('duplicate slots invalidate the entire service, including the other pack', () => {
    const view = batteryView([service([channel(1, sample({ percent: 10 })), channel(2, sample({ percent: 90 })), channel(1, sample({ percent: 80 }))])]);
    assert.equal(view.error, 'invalid-configuration');
    assert.equal(view.packs[0].reason, 'invalid-configuration');
    assert.equal(view.packs[0].percent, null);
    assert.equal(view.packs[0].voltageMv, null);
    assert.equal(view.packs[0].presence, 'unknown');
    assert.equal(batteryPresentation(view.packs[0]).value, 'Unavailable');
    assert.equal(view.packs[1].percent, null);
});

test('malformed channel JSON and mismatched wire identity produce an unknown declared slot', () => {
    for (const raw of ['{', 'null', '[]', '5', JSON.stringify({ id: 2, sample: sample() }),
        JSON.stringify({ id: 1, sample: null })]) {
        const malformed = channel(1);
        malformed.controls[1].value = raw;
        const pack = batteryView([service([malformed])]).packs[0];
        assert.equal(pack.id, 1);
        assert.equal(pack.state, 'unknown');
        assert.equal(pack.percent, null);
        assert.equal(pack.voltageMv, null);
    }
});

test('missing or ambiguous slot controls do not create a phantom battery', () => {
    const ambiguous = channel(1); ambiguous.controls.push({ name: 'slot', value: 2 });
    assert.deepEqual(batteryView([service([channel(0), channel(3), channel('1'), ambiguous])]),
        { packs: [], error: 'invalid-configuration' });
    assert.deepEqual(batteryView([service([{ type: 'BatteryChannel', controls: null }])]),
        { packs: [], error: 'invalid-configuration' });
});

test('invalid channel and unsupported child hide other readings without refreshing receipt age', () => {
    for (const invalid of [channel(3), { type: 'OtherService' }, {}, { type: null }, null, 'BatteryChannel']) {
        const receipts = batteryReceiptTracker(12000), good = channel(1);
        good.controls[1].value = JSON.stringify({ id: 1, sample: sample(), revision: 1 });
        assert.equal(batteryView([service([good])], { receipts, now: 0 }).packs[0].percent, 50);
        good.controls[1].value = JSON.stringify({ id: 1, sample: sample(), revision: 2 });
        const broken = batteryView([service([good, invalid])], { receipts, now: 10000 });
        assert.equal(broken.error, 'invalid-configuration');
        assert.equal(broken.packs[0].percent, null);
        good.controls[1].value = JSON.stringify({ id: 1, sample: sample(), revision: 1 });
        assert.equal(batteryView([service([good])], { receipts, now: 12001 }).packs[0].reason, 'telemetry-timeout');
    }
});

test('fresh present zero percent is a real empty battery, distinct from absent', () => {
    const empty = batteryPresentation(one(sample({ percent: 0, voltageMv: 3000 })));
    assert.equal(empty.kind, 'empty');
    assert.equal(empty.value, '~0%');
    assert.equal(empty.fillPercent, 0);
    assert.match(empty.valueLabel, /Approximately 0 percent/);
    const absent = one(sample({ presence: 'absent', percent: 0 }));
    assert.equal(absent.percent, null);
    assert.equal(absent.voltageMv, null);
    assert.equal(batteryPresentation(absent).value, 'Not installed');
    assert.equal(batteryPresentation(absent).fillPercent, null);
});

test('unknown presence never turns a fresh voltage into a charge level', () => {
    const pack = one(sample({ presence: 'unknown', percent: 100 }));
    assert.equal(pack.voltageMv, 4100);
    assert.equal(pack.percent, null);
    const text = batteryPresentation(pack);
    assert.equal(text.kind, 'unknown');
    assert.equal(text.fillPercent, null);
    assert.match(text.detail, /4\.10 V/);
    assert.doesNotMatch(text.description, /percent|100%/);
});

test('stale readings keep only the last voltage and age, with no charge fill', () => {
    const pack = one(sample({ state: 'stale', percent: 60, ageMs: 22000 }));
    assert.equal(pack.percent, null);
    assert.equal(pack.voltageMv, 4100);
    const text = batteryPresentation(pack);
    assert.equal(text.kind, 'stale');
    assert.equal(text.fillPercent, null);
    assert.match(text.detail, /Last 4\.10 V.*22 s ago/);
    assert.doesNotMatch(text.description, /60%/);
});

test('unknown and unavailable measurements do not claim an empty battery', () => {
    const unknown = batteryPresentation(one(sample({ presence: 'unknown', state: 'unknown' })));
    assert.equal(unknown.value, 'Unknown');
    assert.equal(unknown.fillPercent, null);
    const unavailable = batteryPresentation(one(sample({ state: 'unknown' })));
    assert.equal(unavailable.kind, 'unavailable');
    assert.equal(unavailable.value, 'Unavailable');
    assert.equal(unavailable.fillPercent, null);
});

test('percentage, voltage and age require bounded integers without coercion', () => {
    for (const overrides of [{ percent: -1 }, { percent: 101 }, { percent: 1.5 }, { percent: '50' },
        { voltageMv: 0 }, { voltageMv: 65536 }, { voltageMv: '4100' }, { voltageMv: NaN },
        { ageMs: -1 }, { ageMs: 0x100000000 }, { ageMs: '0' }]) {
        const pack = one(sample(overrides));
        assert.equal(pack.percent, null);
        assert.equal(batteryPresentation(pack).fillPercent, null);
    }
    assert.equal(one(sample({ percent: 100, voltageMv: 65535, ageMs: 0xffffffff })).percent, 100);
    assert.equal(one(sample({ voltageMv: 1 })).voltageMv, 1);
});

test('charge percentages require an explicit voltage-estimate source and real measurement metadata', () => {
    for (const overrides of [{ estimate: null }, { estimate: 'unknown' }, { voltageMv: null }, { ageMs: null }])
        assert.equal(one(sample(overrides)).percent, null);
    const text = batteryPresentation(one(sample()));
    assert.equal(text.value, '~50%');
    assert.match(text.detail, /Voltage estimate/);
    assert.match(text.description, /Approximately 50 percent/);
});

test('charging is never inferred from USB or unrecognised status data', () => {
    const raw = channel(1); raw.controls[1].value = JSON.stringify({ id: 1, sample: sample(), charging: 'charging', usb: true });
    const pack = batteryView([service([raw])]).packs[0];
    assert.equal(pack.charging, 'unknown');
    assert.match(batteryPresentation(pack).description, /Charging state unknown/);
});

class Element {
    constructor(tag, namespace = null) { this.tagName = tag; this.namespace = namespace; this.children = []; this.attributes = {}; this.className = ''; this.text = ''; }
    setAttribute(name, value) { this.attributes[name] = String(value); }
    append(...children) { this.children.push(...children); }
    set textContent(value) { this.text = String(value); this.children = []; }
    get textContent() { return this.text + this.children.map(child => child.textContent).join(''); }
    set innerHTML(_) { throw new Error('Status rendering must not consume HTML'); }
}
const document = { createElement: tag => new Element(tag), createElementNS: (namespace, tag) => new Element(tag, namespace) };
const walk = element => [element, ...element.children.flatMap(walk)];

test('invalid configuration without a valid slot still renders a visible status', () => {
    const result = renderBatteryStatus(document, { packs: [], error: 'invalid-configuration' });
    assert.equal(result.tagName, 'ul');
    assert.match(result.textContent, /Battery configuration error/);
});

test('an enabled channel without an identity stays visible as unavailable', () => {
    const incomplete = { type: 'BatteryChannel', enabled: true, controls: [] };
    const view = batteryView([service([channel(1), incomplete])]);
    const result = renderBatteryStatus(document, view);
    assert.match(result.textContent, /Battery 1Unavailable/);
    assert.match(result.textContent, /Battery configuration error/);
    const onlyIncomplete = renderBatteryStatus(document, batteryView([service([incomplete])]));
    assert.match(onlyIncomplete.textContent, /Battery configuration error/);
});

test('battery DOM is a labelled text-backed status list with decorative SVG and no interactive controls', () => {
    assert.equal(renderBatteryStatus(document, null), null);
    const element = renderBatteryStatus(document, batteryView([service([channel(1)])]));
    assert.equal(element.tagName, 'ul');
    assert.equal(element.attributes['aria-label'], 'Battery status');
    assert.match(element.textContent, /Battery 1~50%Voltage estimate/);
    const nodes = walk(element);
    const chip = nodes.find(node => node.attributes['data-battery-id'] === '1');
    assert.match(chip.attributes['aria-label'], /Battery 1: Approximately 50 percent.*Charging state unknown/);
    const svg = nodes.find(node => node.tagName === 'svg');
    assert.equal(svg.namespace, 'http://www.w3.org/2000/svg');
    assert.equal(svg.attributes['aria-hidden'], 'true');
    assert.equal(nodes.some(node => node.tagName === 'button'), false);
    assert.ok(nodes.some(node => node.attributes['aria-label'] === 'Approximately 50 percent'));
});

test('empty and absent icons have different shapes; stale and unknown icons never fill', () => {
    for (const reading of [sample({ percent: 0 }), sample({ presence: 'absent' }), sample({ state: 'stale' }), sample({ presence: 'unknown' })]) {
        const element = renderBatteryStatus(document, batteryView([service([channel(1, reading)])]));
        const nodes = walk(element);
        assert.equal(nodes.some(node => node.attributes.class === 'helio-battery-fill'), false);
        assert.equal(nodes.some(node => node.attributes.class === 'helio-battery-absent-mark'), reading.presence === 'absent');
    }
});

test('battery normalization does not mutate its source and handles cyclic object references', () => {
    const input = [service([channel(1)])]; const before = JSON.stringify(input);
    batteryView(input); assert.equal(JSON.stringify(input), before);
    const cyclic = { children: [] }; cyclic.children.push(cyclic, service([channel(1)]));
    assert.equal(batteryView([cyclic]).packs[0].percent, 50);
});
