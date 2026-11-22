import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const source = readFileSync(new URL('../../src/ui/helio-timer.js', import.meta.url), 'utf8');
const timer = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
const { timeFromMinutes, minutesFromTime, toggleDay, timerView, timerSummary, locationText, renderTimerBody } = timer;

const module = (controls = {}, enabled = true) => ({
    name: 'Timer', type: 'TimerService', enabled,
    controls: Object.entries({ turnOn: 1, onTime: 1080, turnOff: 0, offTime: 1380, days: 127, location: 0,
        latitude: 5252, longitude: 1341, timezone: '<+01>-1<+02>,M3.5.0/2,M10.5.0/3', clock: 'Fri 13:00',
        sun: 'Sunrise 07:43, sunset 16:00', next: 'Turns on today 16:00', ...controls })
        .map(([name, value]) => ({ name, value })),
});

test('a time of day reads and writes as hours and minutes', () => {
    assert.equal(timeFromMinutes(0), '00:00');
    assert.equal(timeFromMinutes(1085), '18:05');
    assert.equal(timeFromMinutes(5000), '23:59');
    assert.equal(minutesFromTime('18:05'), 1085);
    assert.equal(minutesFromTime('7:30'), 450);
    assert.equal(minutesFromTime('24:00'), null);
    assert.equal(minutesFromTime('noon'), null);
});

test('a day chip flips its own bit, Sunday in bit 0', () => {
    assert.equal(toggleDay(127, 0), 126);
    assert.equal(toggleDay(0, 5), 32);
    const view = timerView(module({ days: 0b0100001 }));
    assert.deepEqual(view.dayChips.filter(chip => chip.on).map(chip => chip.label), ['Fr', 'Su']);
    assert.equal(view.dayChips[0].label, 'Mo');
});

test('the closed card says in one line when the lights go on and off', () => {
    assert.equal(timerSummary(timerView(module())), 'sunset to 23:00');
    assert.equal(timerSummary(timerView(module({ turnOn: 0, turnOff: 1 }))), '18:00 to sunrise');
    assert.equal(timerSummary(timerView(module({}, false))), 'Off');
    assert.equal(timerSummary(null), 'Off');
});

test('the location says where it came from, or that it is still being found', () => {
    assert.equal(locationText(timerView(module())), 'Found from your internet connection: 52.52°, 13.41°');
    assert.equal(locationText(timerView(module({ location: 1 }))), 'Entered by hand: 52.52°, 13.41°');
    assert.match(locationText(timerView(module({ timezone: '' }))), /Finding your location/);
    assert.match(locationText(timerView(module({ timezone: '', location: 1 }))), /Enter your place/);
});

// A document just big enough for the card: elements with children, attributes, listeners and a click.
function fakeDocument() {
    const make = tag => {
        const node = { tagName: tag.toUpperCase(), children: [], attributes: {}, dataset: {}, listeners: {}, className: '', textContent: '', value: '', checked: false,
            append(...items) { this.children.push(...items); }, appendChild(item) { this.children.push(item); return item; },
            setAttribute(name, value) { this.attributes[name] = value; },
            addEventListener(type, fn) { this.listeners[type] = fn; } };
        return node;
    };
    return { createElement: make };
}
const all = node => [node, ...node.children.flatMap(all)];
const byText = (root, text) => all(root).find(node => node.textContent === text);

test('a switched-off timer shows only its switch, which turns it on', async () => {
    const sent = [];
    const body = renderTimerBody(fakeDocument(), module({}, false), { send: async (c, v) => sent.push([c, v]), rerender() {} });
    const toggle = all(body).find(node => node.type === 'checkbox');
    assert.equal(all(body).some(node => node.className === 'helio-timer-days'), false);
    toggle.checked = true;
    await toggle.listeners.change();
    assert.deepEqual(sent, [['enabled', true]]);
});

test('the card sends each choice as the control the firmware reads', async () => {
    const sent = [];
    let redrawn = 0;
    const body = renderTimerBody(fakeDocument(), module(), { send: async (c, v) => sent.push([c, v]), rerender() { redrawn++; } });
    await byText(body, 'At a time').listeners.click();
    await byText(body, 'Sa').listeners.click();
    const off = all(body).find(node => node.type === 'time');
    off.value = '22:30';
    await off.listeners.change();
    assert.deepEqual(sent, [['turnOn', 0], ['days', 63], ['offTime', 1350]]);
    assert.equal(redrawn, 3);
    assert.equal(all(body).find(node => node.dataset.timerField === 'next').textContent, 'Turns on today 16:00');
});

test('entering the place by hand sends hundredths of a degree and the zone as typed', async () => {
    const sent = [];
    const body = renderTimerBody(fakeDocument(), module({ location: 1 }), { send: async (c, v) => sent.push([c, v]), rerender() {} });
    const [lat] = all(body).filter(node => node.type === 'number');
    lat.value = '48.137';
    await lat.listeners.change();
    const zone = all(body).find(node => node.className === 'helio-timer-zone');
    zone.value = ' EST5EDT,M3.2.0,M11.1.0 ';
    await zone.listeners.change();
    assert.deepEqual(sent, [['latitude', 4814], ['timezone', 'EST5EDT,M3.2.0,M11.1.0']]);
});
