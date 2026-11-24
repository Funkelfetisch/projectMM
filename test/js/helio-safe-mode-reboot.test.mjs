import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const app = readFileSync(new URL('../../src/ui/helio-app.js', import.meta.url), 'utf8');

function functionSource(name) {
    const match = new RegExp(`^function ${name}\\(`, 'm').exec(app);
    assert.ok(match, `${name} exists in helio-app.js`);
    return app.slice(match.index, app.indexOf('\n}', match.index) + 2);
}

const context = vm.createContext({});
vm.runInContext(['safeModeNotice', 'rebootHidden'].map(functionSource).join('\n'), context);
const { safeModeNotice, rebootHidden } = context;

const status = 'Safe mode: the power gave out 3 times in a row at about 1450 mA of LEDs. Connect a stronger supply, or limit the current to 1150 mA, then restart';
const system = (cause, limit) => ({ status, controls: [{ name: 'uptime', value: '0:00:12' },
    ...(cause ? [{ name: 'safeModeCause', value: cause }, { name: 'suggestedMaxCurrent', value: limit }] : [])] });

test('a supply that gave out is explained, with the device status and the suggested current limit', () => {
    const notice = safeModeNotice(system('power', 1150));
    assert.equal(notice.title, 'The power supply gave out');
    assert.equal(notice.text, status);
    assert.equal(notice.limit, 1150);
});

test('a crash loop is explained without a current limit, which would not help', () => {
    const notice = safeModeNotice(system('crash', 0));
    assert.equal(notice.title, 'The device kept crashing');
    assert.equal(notice.limit, 0);
});

test('a power failure before the LEDs drew much offers no limit either', () => {
    assert.equal(safeModeNotice(system('power', 0)).limit, 0);
});

test('a device running normally shows no warning', () => {
    assert.equal(safeModeNotice(system(null)), null);
    assert.equal(safeModeNotice(undefined), null);
});

test('a HELIO in safe mode shows the reboot button, its one way out without unplugging', () => {
    assert.equal(rebootHidden(true, 'ESP32-S3', true), false);
    assert.equal(rebootHidden(true, 'ESP32-S3', false), true);
});

test('the desktop build hides the reboot button in every mode, since its reboot exits the process', () => {
    assert.equal(rebootHidden(false, 'desktop', true), true);
    assert.equal(rebootHidden(false, 'ESP32-S3', false), false);
});
