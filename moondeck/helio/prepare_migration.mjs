#!/usr/bin/env node
// Prepare an offline product migration with MoonLight's own rename engine.
// Only non-network product configuration is read; originals remain untouched.
import { readFileSync, writeFileSync, mkdirSync, existsSync } from 'node:fs';
import path from 'node:path';
import { applyMigrations } from '../../src/ui/migrate.js';

const args = process.argv.slice(2);
const option = name => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : null; };
const input = option('--input'), livePath = option('--live'), out = option('--out');
if (!input || !livePath || !out) throw new Error('Use --input <protected config directory> --live <module-tree.json> --out <new directory>');
const live = JSON.parse(readFileSync(livePath, 'utf8'));
if (!Array.isArray(live)) throw new Error('--live must contain the product module array');
const files = {};
for (const name of ['Drivers', 'Layouts', 'Effects', 'Services']) {
    const filename = path.join(input, name + '.json');
    if (!existsSync(filename)) throw new Error('Missing required product config: ' + name);
    files['/.config/' + name + '.json'] = readFileSync(filename, 'utf8');
}
const { files: migrated, report } = applyMigrations(files, live);
if (report.some(row => row.kind === 'review')) throw new Error('Migration requires review: ' + JSON.stringify(report.filter(row => row.kind === 'review')));
const state = {};
for (const content of Object.values(migrated)) Object.assign(state, JSON.parse(content));
const drivers = state.Drivers;
if (!drivers) throw new Error('Drivers root absent');
for (const child of Object.values(drivers)) {
    if (!child || typeof child !== 'object') continue;
    if (child.type === 'PreviewDriver' && child.resumableFrames !== undefined) {
        delete child.resumableFrames;
        report.push({kind:'retired',where:'Drivers/Preview',detail:'resumable preview is intrinsic to the new pull transport'});
    }
    if (child.type !== 'RmtLedDriver') continue;
    // The old durable name wins over its obsolete numeric picker index.
    if (typeof child.fixtureRef === 'string' && child.fixtureRef) { child.fixture = child.fixtureRef; delete child.fixtureRef; }
    if (child.timingProfile !== undefined) {
        const timing = child.timingProfile;
        if (timing === 0) child.timing = '800kHz WS2812B/SK6812';
        else if (timing === 1) child.timing = '800kHz SK6812 RGBW';
        else if (timing === 2 || timing === 3) {
            child.timing = 'custom';
            [child.t0hNs, child.t1hNs, child.periodNs] = timing === 2 ? [300, 900, 1200] : [400, 800, 1250];
        } else throw new Error('Unknown legacy timing profile');
        delete child.timingProfile;
        report.push({kind:'renamed',where:'Drivers/RmtLed',detail:'legacy HELIO timing profile -> named MoonLight timing'});
    }
}
// Restore the shared library before a driver's named Select resolves its rows.
const orderedDrivers = {};
for (const [key, value] of Object.entries(drivers)) if (value?.type === 'FixtureProfilesModule') orderedDrivers[key] = value;
for (const [key, value] of Object.entries(drivers)) if (value?.type !== 'FixtureProfilesModule') orderedDrivers[key] = value;
state.Drivers = orderedDrivers;
const originalOn = drivers.on;
state.Drivers.on = false;
mkdirSync(out, { recursive: true, mode: 0o700 });
const write = (filename, object) => writeFileSync(path.join(out, filename), JSON.stringify(object, null, 2) + '\n', {flag:'wx',mode:0o600});
write('state-off.json', state);
// A complete tree can exceed the S3's contiguous JSON arena while BLE is live.
// These are ordered, independently acknowledged writes, with output held off.
const order = ['Drivers', 'Layouts', 'Effects', 'Services'];
write('state-off-chunks.json', order.map(name => ({[name]: state[name]})));
write('restore-power.json', { Drivers: { on: originalOn } });
write('migration-report.json', { source: path.resolve(input), sourceBuild:'caller must bind to the captured live identity', originalPower:originalOn, roots:Object.keys(state), applyOrder:order, compactChunkBytes:order.map(name => Buffer.byteLength(JSON.stringify({[name]:state[name]}))), application:'Confirm persisted output-off before applying compact chunks; stop on error, verify every root before restoring power. The sequence is not atomic.', changes:report, ignoredInactiveLegacyLayers:existsSync(path.join(input,'Layers.json')) });
console.log('Prepared product state with output off; no device or source file changed.');
