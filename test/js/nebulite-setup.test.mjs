import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
const load = async file => import('data:text/javascript;base64,' + Buffer.from(readFileSync(new URL(file, import.meta.url), 'utf8')).toString('base64'));
const { compatibleProductProfiles, productProfilePatch, productProfileMatches, productProjectionProfile, productProjectionPoints, productProjection, renderProductSetup } = await load('../../src/ui/nebulite-setup.js');
const { NEBULITE_SETUP_PROFILES: profiles } = await load('../../src/ui/nebulite-profile-data.js');
const identity = { firmware: 'nebulite-s3', chip: 'ESP32-S3 (rev 0.2)' };
const profile = profiles.find(item => item.name === 'NEBULITE Croptop S3');
const clone = value => JSON.parse(JSON.stringify(value));
// What setup applies when the owner picks no layout: the 2D one where the product has it, its strip otherwise.
const defaultSetup = p => { try { return productProjectionProfile(p, 'virtual2d'); } catch { return productProjectionProfile(p, 'strip'); } };
// A wiring change that still counts, now that a moved pin stays the product: a different light count.
const changeWiring = modules => { modules[1].children[0].controls.find(control => control.name === 'ledsPerPin').value = '9'; return modules; };
function modulesFor(p = profile) {
    return [
        { name: 'System', controls: [{ name: 'deviceModel', value: p.name }] },
        ...['Drivers', 'Layouts'].map(name => ({
            name, children: Object.entries(p.state[name]).filter(([, item]) => item?.type).map(([name, item]) => ({
                name, type: item.type, role: 'driver',
                controls: Object.entries(item).filter(([key]) => !['type', '$patch'].includes(key)).map(([name, value]) => ({ name, value })),
            })),
        })),
    ];
}
test('NEBULITE product choices derive output state from the canonical catalogue', () => {
    const catalog = JSON.parse(readFileSync(new URL('../../mooninstaller/deviceModels.json', import.meta.url)));
    assert.equal(profiles.length, catalog.filter(model => model.firmwares.some(firmware => ['nebulite-s3', 'nebulite-esp32'].includes(firmware))).length);
    for (const p of profiles) {
        const model = catalog.find(model => model.name === p.name);
        assert.deepEqual(p.state, Object.fromEntries(['System', 'Drivers', 'Layouts'].map(key => [key, model.state[key]])));
    }
});
test('product setup requires the actual firmware and chip family', () => {
    assert.deepEqual(compatibleProductProfiles(profiles, identity).map(p => p.name), ['NEBULITE Croptop S3', 'NEBULITE Jacket S3', 'HELIO necklace']);
    for (const wrong of [{ firmware: 'helio1', chip: 'ESP32-S3' }, { firmware: 'nebulite-s3', chip: 'ESP32' },
        { firmware: 'nebulite-s3', chip: 'ESP32-S32' }, { firmware: 'nebulite-s3' }, {}]) {
        assert.deepEqual(compatibleProductProfiles(profiles, wrong), []);
        assert.throws(() => productProfilePatch(profile, wrong), /does not match/);
    }
});
test('product setup patches only model outputs and layout using detached data', () => {
    const p = clone(profile);
    p.state.Services = { BLE: { enabled: false }, Battery: { adcPin: 15 } };
    p.state.Network = { WiFi: { enabled: false } }; p.state.Effects = { Scene: null };
    const patch = productProfilePatch(p, identity);
    assert.deepEqual(Object.keys(patch), ['System', 'Drivers', 'Layouts']);
    assert.deepEqual(patch.System, { deviceModel: profile.name });
    assert.equal(patch.Drivers.$patch, 'replace');
    const first = Object.keys(patch.Drivers).find(key => key !== '$patch');
    assert.equal(patch.Drivers[first].enabled, true);
    assert.equal(patch.Layouts.ProductStrip.enabled, true);
    patch.Drivers[first].pins = '99';
    assert.notEqual(p.state.Drivers[first].pins, '99');
});
test('virtual 2D setup preserves output wiring and confirms its exact chosen raster', () => {
    for (const name of ['NEBULITE Croptop S3', 'NEBULITE Jacket S3']) {
        const base = profiles.find(item => item.name === name);
        const virtual = productProjectionProfile(base, 'virtual2d');
        const patch = productProfilePatch(virtual, identity);
        assert.equal(patch.Layouts.ProductStrip.type, 'PixelMapLayout');
        assert.equal(patch.Layouts.ProductStrip.points.split(';').length, name === 'NEBULITE Croptop S3' ? 9 : 31);
        assert.deepEqual(patch.Drivers.RGB1, productProfilePatch(base, identity).Drivers.RGB1);
        assert.deepEqual(patch.Drivers.Accent, productProfilePatch(base, identity).Drivers.Accent);
        if (name === 'NEBULITE Jacket S3') { assert.equal(patch.Drivers.RGB3, undefined); assert.equal(patch.Drivers.RGB4, undefined); }
        assert.equal(productProjection(base, modulesFor(virtual)), 'virtual2d');
        assert.equal(productProfileMatches(base, modulesFor(virtual)), true);
        assert.equal(productProfileMatches(virtual, modulesFor(base)), false);
        assert.equal(productProfileMatches(productProjectionProfile(base, 'strip'), modulesFor(virtual)), false);
        assert.equal(base.state.Layouts.ProductStrip.height, 1);
    }
});
test('the provisional jacket map follows the stated bottom, top, hood and sleeves sequence', () => {
    const base = profiles.find(p => p.name === 'NEBULITE Jacket S3');
    const {points} = productProjectionPoints(base);
    assert.equal(points.length, 31);
    assert(points.slice(0,6).every(([,y]) => y === 14));
    assert(points.slice(6,12).every(([,y]) => y === 6));
    assert(points.slice(12,18).every(([,y]) => y <= 2));
    assert(points.slice(18,24).every(([x]) => x <= 3));
    assert(points.slice(24,30).every(([x]) => x >= 17));
    assert.deepEqual(points[30], [10,10]); // independent PWM output
});
test('edited point maps remain recognized but never satisfy exact setup verification', () => {
    const base = profiles.find(p=>p.name==='NEBULITE Jacket S3');
    const candidate = productProjectionProfile(base,'virtual2d');
    const modules = modulesFor(candidate);
    const pointControl = modules[2].children[0].controls.find(c=>c.name==='points');
    const original = pointControl.value;
    pointControl.value = original.replace('5,14;', '6,14;');
    assert.equal(productProjection(base,modules),'virtual2d');
    assert.equal(productProfileMatches(base,modules),true);
    assert.equal(productProfileMatches(candidate,modules),false);
    assert.deepEqual(productProjectionPoints(base,modules).points[0],[6,14]);
    assert.deepEqual(productProjectionPoints(base).points[0],[5,14]);
    pointControl.value += ';0,0'; assert.equal(productProjection(base,modules),null);
    pointControl.value = original.replace('5,14;', '99,14;'); assert.equal(productProjection(base,modules),null);
});
test('preview geometry survives output corrections and a moved pin without weakening setup verification', () => {
    for (const name of ['NEBULITE Croptop S3', 'NEBULITE Jacket S3']) {
        const base = profiles.find(p => p.name === name);
        const candidate = productProjectionProfile(base, 'virtual2d');
        const modules = modulesFor(candidate);
        const rgb = modules[1].children.find(child => child.name === 'RGB1');
        rgb.controls.find(control => control.name === 'curve').value = 'gamma 2.2';
        assert.equal(productProjection(base, modules), 'virtual2d');
        assert.equal(productProfileMatches(base, modules), true);
        assert.equal(productProfileMatches(candidate, modules), false);
        // The profile's pin is a default the owner may move, as to a bench string on another pad.
        rgb.controls.find(control => control.name === 'pins').value = '34';
        assert.equal(productProjection(base, modules), 'virtual2d');
        assert.equal(productProfileMatches(base, modules), true);
    }
});
test('disabled output, layout or owning group is never reported as a working product setup', () => {
    for (const path of [[1], [2], [1, 'children', 0], [2, 'children', 0]]) {
        const modules = modulesFor();
        const target = path.reduce((value, key) => value[key], modules);
        target.enabled = false;
        assert.equal(productProfileMatches(profile, modules), false, path.join('.'));
    }
});
test('a product name alone cannot confirm wiring, a moved pin stays the product, and extra outputs are custom setup', () => {
    const modules = modulesFor();
    assert.equal(productProfileMatches(profile, [modules[0]]), false);
    assert.equal(productProfileMatches(profile, modules), true);
    const pins = modules[1].children[0].controls.find(control => control.name === 'pins');
    pins.value = '99'; assert.equal(productProfileMatches(profile, modules), true);
    pins.value = profile.state.Drivers[modules[1].children[0].name].pins;
    modules[1].children.push({ name: 'Extra', type: 'RmtLedDriver', role: 'driver', controls: [] });
    assert.equal(productProfileMatches(profile, modules), false);
});

test('inactive extra drivers and layouts do not hide a matching preview', () => {
    const modules = modulesFor(profile);
    const drivers = modules.find(m => m.name === 'Drivers');
    const layouts = modules.find(m => m.name === 'Layouts');
    const extraDriver = { name: 'spare', type: 'RmtLedDriver', role: 'driver', enabled: false };
    const extraLayout = { name: 'spare', type: 'SingleRowLayout', enabled: false };
    drivers.children.push(extraDriver);
    layouts.children.push(extraLayout);
    assert.equal(productProfileMatches(profile, modules), true);
    assert.equal(productProjection(profile, modules), 'strip');
    const candidate = productProjectionProfile(profile, 'virtual2d');
    const projected = modulesFor(candidate);
    projected.find(m => m.name === 'Drivers').children.push({ ...extraDriver });
    assert.equal(productProfileMatches(profile, projected), true);
    assert.equal(productProfileMatches(candidate, projected), false, 'exact setup verification still notices the extra driver');
    extraDriver.enabled = true;
    assert.equal(productProfileMatches(profile, modules), false);
    extraDriver.enabled = false;
    extraLayout.enabled = true;
    assert.equal(productProfileMatches(profile, modules), false);
});
test('profile readback resolves select labels and ignores preview infrastructure', () => {
    const modules = modulesFor();
    for (const child of modules[1].children) for (const control of child.controls) {
        if (control.name === 'whiteMode') { control.options = ['None', 'Min', 'Accurate']; control.value = control.options.indexOf(control.value); }
        if (control.name === 'curve') { control.options = ['CIE 1931', 'gamma 2.2', 'gamma 2.8', 'linear']; control.value = control.options.indexOf(control.value); }
    }
    modules[1].children.push({ name: 'Preview', type: 'PreviewDriver', role: 'driver' }, { name: 'FixtureProfiles', type: 'FixtureProfilesModule', role: 'generic' });
    assert.equal(productProfileMatches(profile, modules), true);
    modules[2].children[0].controls.find(control => control.name === 'width').value++;
    assert.equal(productProfileMatches(profile, modules), false);
});
class Element {
    constructor(tag) { this.tagName = tag; this.children = []; this.attributes = {}; this.events = {}; this.value = ''; this.disabled = false; }
    append(...nodes) { this.children.push(...nodes); }
    setAttribute(key, value) { this.attributes[key] = value; }
    addEventListener(name, listener) { this.events[name] = listener; }
    async fire(name) { await this.events[name]?.({ target: this }); }
}
function elements(node, tag) { return [node, ...node.children.flatMap(child => child instanceof Element ? elements(child, tag) : [])].filter(child => child.tagName === tag); }
function mount(options = {}) {
    globalThis.document = { createElement: tag => new Element(tag) };
    return renderProductSetup({ profiles, identity, modules: [], apply: async () => {}, verify: async () => true, ...options });
}
test('setup explains that applying it replaces custom outputs and LED positions', () => {
    const view = mount();
    assert(elements(view, 'p').some(node => /replaces the current light outputs and layout.*custom wiring and LED positions/i.test(node.textContent)));
    assert(elements(view, 'p').some(node => /Battery settings are kept; check for pin conflicts/i.test(node.textContent)));
    for (const p of compatibleProductProfiles(profiles, identity)) {
        for (const projection of ['strip', 'virtual2d']) {
            assert.deepEqual(Object.keys(productProfilePatch(productProjectionProfile(p, projection), identity)).sort(),
                ['Drivers', 'Layouts', 'System']);
        }
    }
});
test('classic crop-top setup is opt-in, chip-specific and repeats the archived eight-output map on both LED sockets', async () => {
    const classic = profiles.find(p => p.name === 'NEBULITE Croptop ESP32');
    const classicIdentity = { firmware: 'nebulite-esp32', chip: 'ESP32 (rev 3)' };
    assert.deepEqual(compatibleProductProfiles(profiles, classicIdentity).map(p => p.name),
        ['NEBULITE Croptop ESP32', 'NEBULITE Fanny pack ESP32', 'NEBULITE Backpack ESP32', 'NEBULITE Jacket ESP32']);
    assert.equal(compatibleProductProfiles(profiles, { ...classicIdentity, chip: 'ESP32-S3' }).length, 0);
    let written;
    const view = mount({ identity: classicIdentity, apply: async patch => { written = patch; } });
    const [product, projection] = elements(view, 'select');
    product.value = classic.name; await product.fire('change');
    assert(elements(view, 'p').some(node => /GPIO26, mirrored on GPIO17.*Battery and charging wiring are not configured/.test(node.textContent)));
    projection.value = 'virtual2d'; await projection.fire('change');
    await elements(view, 'button')[0].fire('click');
    assert.deepEqual([written.Drivers.RGB1.pins, written.Drivers.RGB2.pins], ['26', '17']);
    assert.equal(written.Drivers.RGB2.start, written.Drivers.RGB1.start);
    assert.deepEqual(Object.keys(written.Drivers).sort(), ['$patch', 'RGB1', 'RGB2']);
    assert.equal(written.Layouts.ProductStrip.points.split(';').length, 8);
});
test('S3 setup warns that a matching chip does not establish board wiring', async () => {
    const view = mount();
    const [product] = elements(view, 'select');
    product.value = 'NEBULITE Croptop S3'; await product.fire('change');
    assert(elements(view, 'p').some(node => /mirrored on GPIO36, 35, 48 and 34.*verify GPIO46.*Matching the chip does not verify the board wiring/.test(node.textContent)));
    product.value = 'NEBULITE Jacket S3'; await product.fire('change');
    assert(elements(view, 'p').some(node => /verify the GPIOs.*Matching the chip does not verify the board wiring.*parallel branches/.test(node.textContent)));
});
test('setup offers the 2D layout first, mirrors the Croptop S3 strip on every LED pad, and gives a product without 2D its strip', async () => {
    let written;
    const view = mount({ apply: async patch => { written = patch; } });
    const [product, projection] = elements(view, 'select');
    product.value = 'NEBULITE Croptop S3'; await product.fire('change');
    assert.equal(projection.value, 'virtual2d');
    await elements(view, 'button')[0].fire('click');
    assert.equal(written.Layouts.ProductStrip.type, 'PixelMapLayout');
    const strips = ['RGB1', 'RGB2', 'RGB3', 'RGB4'];
    assert.deepEqual(strips.map(name => written.Drivers[name].pins), ['36', '35', '48', '34']);
    assert(strips.every(name => written.Drivers[name].start === 0 && written.Drivers[name].ledsPerPin === '8'));
    const classicIdentity = { firmware: 'nebulite-esp32', chip: 'ESP32 (rev 3)' };
    const classic = mount({ identity: classicIdentity, apply: async patch => { written = patch; } });
    const [fanny] = elements(classic, 'select');
    fanny.value = 'NEBULITE Fanny pack ESP32'; await fanny.fire('change');
    await elements(classic, 'button')[0].fire('click');
    assert.equal(written.Layouts.ProductStrip.type, 'GridLayout');
});
test('setup applies and verifies the selected virtual 2D profile once', async () => {
    let written, checked;
    const view = mount({apply: async patch => { written = patch; }, verify: async selected => { checked = selected; return true; }});
    const [product, projection] = elements(view, 'select');
    product.value = 'NEBULITE Croptop S3'; await product.fire('change');
    projection.value = 'virtual2d'; await projection.fire('change');
    await elements(view, 'button')[0].fire('click');
    assert.equal(written.Layouts.ProductStrip.type, 'PixelMapLayout');
    assert.equal(written.Layouts.ProductStrip.points.split(';').length, 9);
    assert.equal(checked.projection, 'virtual2d');
    assert.equal(elements(view, 'button')[0].textContent, 'Setup saved');
});
test('compact setup waits for metadata before naming existing output wiring', async () => {
    const full = modulesFor(), compact = clone(full);
    for (const child of compact[1].children) for (const control of child.controls) {
        if (control.name === 'whiteMode') control.value = 0;
        if (control.name === 'curve') control.value = 3;
    }
    let current = compact, writes = 0;
    const pending = deferred();
    const view = mount({ modules: compact, getModules: () => current,
        loadMetadata: () => pending.promise, apply: async () => { writes++; } });
    assert.equal(elements(view, 'summary')[0].children[1].textContent, 'Checking setup…');
    current = full; pending.resolve(true);
    await until(() => elements(view, 'summary')[0].children[1].textContent === profile.name);
    assert.equal(elements(view, 'select')[0].value, profile.name);
    assert.equal(writes, 0);
});

test('late metadata does not replace an explicit product selection', async () => {
    const full = modulesFor(), compact = clone(full);
    for (const child of compact[1].children) for (const control of child.controls)
        if (control.name === 'whiteMode' || control.name === 'curve') control.value = 0;
    let current = compact; const pending = deferred();
    const view = mount({ modules: compact, getModules: () => current, loadMetadata: () => pending.promise });
    const select = elements(view, 'select')[0];
    select.value = 'NEBULITE Jacket S3'; await select.fire('change');
    current = full; pending.resolve(true);
    await until(() => elements(view, 'summary')[0].children[1].textContent === profile.name);
    assert.equal(select.value, 'NEBULITE Jacket S3');
});

test('failed metadata loading reports unknown rather than custom wiring', async () => {
    const compact = modulesFor();
    for (const child of compact[1].children) for (const control of child.controls)
        if (control.name === 'whiteMode' || control.name === 'curve') control.value = 0;
    const view = mount({ modules: compact, loadMetadata: async () => false });
    await until(() => elements(view, 'summary')[0].children[1].textContent === 'Setup unknown');
});

test('failed metadata cannot certify a cached match or replay a previous setup write', async () => {
    const modules = modulesFor();
    const session = { attempt: { profileName: profile.name, busy: false, written: true,
        confirmed: true, message: 'Product setup saved.', phase: '', touched: true } };
    let writes = 0;
    const view = mount({ modules, session, loadMetadata: async () => false,
        apply: async () => { writes++; } });
    await until(() => elements(view, 'summary')[0].children[1].textContent === 'Setup unknown');
    assert.equal(elements(view, 'button')[0].textContent, 'Check setup');
    assert.match(elements(view, 'p').find(node => node.attributes.role === 'status').textContent, /could not be verified/);
    await elements(view, 'button')[0].fire('click');
    assert.equal(writes, 0);
    assert.equal(elements(view, 'button')[0].textContent, 'Setup saved');
    assert.equal(elements(view, 'summary')[0].children[1].textContent, profile.name);
});

test('late metadata identifies an existing custom output from the latest state', async () => {
    let current = []; const pending = deferred();
    const view = mount({ modules: current, getModules: () => current, loadMetadata: () => pending.promise });
    assert.equal(elements(view, 'summary')[0].children[1].textContent, 'Checking setup…');
    current = changeWiring(modulesFor());
    pending.resolve(true);
    await until(() => elements(view, 'summary')[0].children[1].textContent === 'Custom setup');
    assert.equal(elements(view, 'select')[0].value, '');
});

test('controllers without compatible profiles do not wait for unused metadata', () => {
    let reads = 0;
    const view = mount({ identity: { firmware: 'nebulite-esp32', chip: 'ESP32-S3' },
        modules: modulesFor(), loadMetadata: async () => { reads++; return true; } });
    assert.equal(elements(view, 'summary')[0].children[1].textContent, 'Custom setup');
    assert.equal(reads, 0);
    assert.equal(elements(view, 'button')[0].disabled, true);
});

test('a previously confirmed setup is invalidated if late metadata shows changed wiring', async () => {
    const full = modulesFor(), changed = changeWiring(clone(full));
    const session = { attempt: { profileName: profile.name, busy: false, written: true,
        confirmed: true, message: 'Product setup saved.', phase: '', touched: true } };
    let current = full; const pending = deferred();
    const view = mount({ modules: full, session, getModules: () => current, loadMetadata: () => pending.promise });
    assert.equal(elements(view, 'button')[0].textContent, 'Setup saved');
    current = changed; pending.resolve(true);
    await until(() => elements(view, 'button')[0].textContent === 'Check setup');
    assert.match(elements(view, 'p').find(node => node.attributes.role === 'status').textContent, /setup has changed/);
});

test('jacket and vest share one visible setup while keeping the stored product identity', () => {
    const jacket = profiles.find(p => p.name === 'NEBULITE Jacket S3');
    assert.ok(jacket);
    const view = mount({ modules: modulesFor(jacket) });
    const options = elements(view, 'option');
    const choice = options.find(option => option.value === jacket.name);
    assert.equal(choice.textContent, 'NEBULITE Jacket / Vest S3');
    assert.equal(options.filter(option => /Jacket|Vest/.test(option.textContent)).length, 1);
    assert.equal(productProfilePatch(jacket, identity).System.deviceModel, 'NEBULITE Jacket S3');
    assert.equal(productProfileMatches(jacket, modulesFor(jacket)), true);
    assert.equal(elements(view, 'summary')[0].children[1].textContent, choice.textContent);
});
test('the HELIO necklace sets up as one GRBW ring whose rail relay is part of the confirmed setup', () => {
    const necklace = profiles.find(p => p.name === 'HELIO necklace');
    const patch = productProfilePatch(necklace, identity);
    assert.equal(patch.Drivers.relayPins, '17');
    assert.deepEqual(Object.keys(patch.Drivers).filter(key => patch.Drivers[key]?.type), ['RmtLed']);
    assert.equal(patch.Layouts.ProductStrip.type, 'PixelMapLayout');
    assert.equal(patch.Layouts.ProductStrip.points.split(';').length, 43);
    const modules = modulesFor(necklace);
    modules[1].controls = [{ name: 'relayPins', value: '17' }];
    assert.equal(productProfileMatches(necklace, modules), true);
    assert.equal(productProjection(necklace, modules), 'virtual2d');
    assert.equal(productProjectionPoints(necklace, modules).width, 15);
    modules[1].controls[0].value = '';
    assert.equal(productProfileMatches(necklace, modules), false);
});
test('unconfigured NEBULITE opens labelled setup without automatic hardware writes', async () => {
    let writes = 0;
    const view = mount({ apply: async () => { writes++; } });
    assert.equal(view.open, true);
    const [select] = elements(view, 'select'), [button] = elements(view, 'button');
    assert.equal(select.attributes['aria-label'], 'NEBULITE product');
    assert.equal(button.disabled, true); assert.equal(writes, 0);
    select.value = profile.name; await select.fire('change'); assert.equal(button.disabled, false);
    assert.equal(elements(view, 'p').find(node => node.attributes.role === 'status').attributes['aria-live'], 'polite');
});
test('product illustrations follow the selected family without changing setup', async () => {
    let writes = 0;
    const view = mount({ apply: async () => { writes++; } });
    const [select] = elements(view, 'select');
    const [figure] = elements(view, 'figure'), [image] = elements(view, 'img');
    assert.equal(figure.hidden, true);
    select.value = 'NEBULITE Croptop S3'; await select.fire('change');
    assert.equal(figure.hidden, false);
    assert.equal(image.src, '/products/croptop_top.png');
    assert.match(image.alt, /Crop-top/);
    select.value = 'NEBULITE Jacket S3'; await select.fire('change');
    assert.equal(image.src, '/products/jacket_leds.png');
    assert.match(image.alt, /vest/);
    select.value = ''; await select.fire('change');
    assert.equal(figure.hidden, true);
    assert.equal(writes, 0);
});
test('existing wiring keeps setup collapsed and unsupported controllers cannot apply', () => {
    assert.equal(mount({ modules: modulesFor() }).open, false);
    const custom = modulesFor(); custom[0].controls[0].value = 'Custom';
    assert.equal(mount({ modules: custom }).open, false);
    const wrong = mount({ identity: { firmware: 'helio1', chip: 'ESP32-S3' } });
    assert.equal(elements(wrong, 'select')[0].children.length, 1);
    assert.equal(elements(wrong, 'button')[0].disabled, true);
});
test('setup sends one write and confirms readback before reporting saved', async () => {
    let writes = 0, checks = 0, release;
    const ready = new Promise(resolve => { release = resolve; });
    const view = mount({ apply: async patch => { writes++; assert.deepEqual(patch, productProfilePatch(defaultSetup(profile), identity)); await ready; },
        verify: async selected => { checks++; assert.equal(selected.name, profile.name); return true; } });
    const [select] = elements(view, 'select'), [button] = elements(view, 'button');
    select.value = profile.name; await select.fire('change');
    const pending = button.fire('click');
    assert.equal(writes, 1); assert.equal(checks, 0); assert.equal(select.disabled, true);
    await button.fire('click'); assert.equal(writes, 1);
    release(); await pending;
    assert.equal(checks, 1); assert.equal(button.textContent, 'Setup saved'); assert.equal(select.disabled, false);
});
test('an uncertain setup response is checked without replaying its write', async () => {
    let writes = 0, checks = 0;
    const view = mount({ apply: async () => { writes++; throw new Error('Connection interrupted.'); },
        verify: async () => { checks++; return true; } });
    const [select] = elements(view, 'select'), [button] = elements(view, 'button');
    select.value = profile.name; await select.fire('change');
    await button.fire('click');
    assert.equal(writes, 1); assert.equal(checks, 0); assert.equal(button.textContent, 'Check setup');
    await button.fire('click');
    assert.equal(writes, 1); assert.equal(checks, 1); assert.equal(button.textContent, 'Setup saved');
});

function deferred() {
    let resolve;
    const promise = new Promise(done => { resolve = done; });
    return { promise, resolve };
}
function controls(view) {
    return { select: elements(view, 'select')[0], button: elements(view, 'button')[0],
        status: elements(view, 'p').find(node => node.attributes.role === 'status'), section: elements(view, 'section')[0] };
}
async function until(predicate) {
    for (let i = 0; i < 30 && !predicate(); i++) await Promise.resolve();
    assert.ok(predicate(), 'the asynchronous setup checkpoint was reached');
}
async function choose(view, name) {
    const { select } = controls(view);
    select.value = name;
    await select.fire('change');
}

test('a shared setup session retains a deliberate selection across remount before any write', async () => {
    const session = {};
    let writes = 0;
    const options = { session, apply: async () => { writes++; } };
    const first = mount(options);
    await choose(first, profile.name);
    const current = controls(mount(options));
    assert.equal(current.select.value, profile.name);
    assert.equal(current.button.disabled, false);
    assert.equal(current.button.textContent, 'Apply product setup');
    assert.equal(writes, 0);
});

test('remounts share a busy apply and verification and its completion updates the current view', async () => {
    const session = {};
    const applied = deferred(), verified = deferred();
    let writes = 0, checks = 0;
    const options = { session,
        apply: async patch => { writes++; assert.deepEqual(patch, productProfilePatch(defaultSetup(profile), identity)); await applied.promise; },
        verify: async selected => { checks++; assert.equal(selected.name, profile.name); return verified.promise; } };
    const first = mount(options);
    await choose(first, profile.name);
    const pending = controls(first).button.fire('click');
    assert.equal(writes, 1);
    const second = controls(mount(options));
    assert.equal(second.select.value, profile.name);
    assert.equal(second.select.disabled, true);
    assert.equal(second.button.disabled, true);
    assert.equal(second.section.attributes['aria-busy'], 'true');
    await second.button.fire('click');
    assert.equal(writes, 1, 'a second view cannot replay the pending hardware write');
    applied.resolve();
    await until(() => checks === 1);
    const current = controls(mount(options));
    assert.equal(current.select.disabled, true);
    assert.equal(current.button.disabled, true);
    await current.button.fire('click');
    assert.equal(checks, 1, 'a remount cannot start a concurrent verification');
    verified.resolve(true);
    await pending;
    assert.equal(current.button.textContent, 'Setup saved');
    assert.equal(current.select.value, profile.name);
    assert.equal(current.select.disabled, false);
    assert.equal(current.section.attributes['aria-busy'], 'false');
    assert.equal(current.status.textContent, 'Product setup saved.');
    assert.equal(writes, 1);
});

test('a lost setup response remains verify-only after remount and its readback updates a newer view', async () => {
    const session = {};
    const verification = deferred();
    let writes = 0, checks = 0;
    const options = { session,
        apply: async () => { writes++; throw new Error('Response lost.'); },
        verify: async selected => { checks++; assert.equal(selected.name, profile.name); return verification.promise; } };
    const first = mount(options);
    await choose(first, profile.name);
    await controls(first).button.fire('click');
    assert.equal(checks, 0);
    const second = controls(mount(options));
    assert.equal(second.select.value, profile.name);
    assert.equal(second.button.textContent, 'Check setup');
    assert.equal(second.button.disabled, false);
    assert.match(second.status.textContent, /not confirmed.*Response lost/s);
    const pending = second.button.fire('click');
    assert.equal(writes, 1);
    assert.equal(checks, 1);
    const current = controls(mount(options));
    assert.equal(current.select.disabled, true);
    assert.equal(current.button.disabled, true);
    verification.resolve(true);
    await pending;
    assert.equal(writes, 1, 'checking an uncertain operation never reapplies it');
    assert.equal(current.button.textContent, 'Setup saved');
    assert.equal(current.status.textContent, 'Product setup saved.');
    assert.equal(current.select.disabled, false);
});

test('an explicit different choice after a settled uncertain attempt allows one deliberate new apply', async () => {
    const other = compatibleProductProfiles(profiles, identity).find(item => item.name !== profile.name);
    assert.ok(other, 'two compatible products exercise a deliberate selection change');
    const session = {};
    const writes = [], checks = [];
    const options = { session,
        apply: async patch => { writes.push(patch); if (writes.length === 1) throw new Error('Response lost.'); },
        verify: async selected => { checks.push(selected.name); return true; } };
    const first = mount(options);
    await choose(first, profile.name);
    await controls(first).button.fire('click');
    const second = mount(options);
    assert.equal(controls(second).button.textContent, 'Check setup');
    await choose(second, other.name);
    assert.equal(controls(second).button.textContent, 'Apply product setup');
    assert.equal(controls(second).button.disabled, false);
    await controls(second).button.fire('click');
    assert.equal(writes.length, 2);
    assert.deepEqual(writes[0], productProfilePatch(defaultSetup(profile), identity));
    assert.deepEqual(writes[1], productProfilePatch(defaultSetup(other), identity));
    assert.deepEqual(checks, [other.name]);
    const current = controls(mount({ ...options, modules: modulesFor(defaultSetup(other)) }));
    assert.equal(current.select.value, other.name);
    assert.equal(current.button.textContent, 'Setup saved');
});

test('fresh externally changed wiring invalidates a confirmed setup instead of keeping false saved status', async () => {
    const session = {};
    let writes = 0, checks = 0;
    const options = { session, apply: async () => { writes++; }, verify: async () => true };
    const first = mount(options);
    await choose(first, profile.name);
    await controls(first).button.fire('click');
    assert.equal(controls(first).button.textContent, 'Setup saved');
    assert.equal(controls(mount({ ...options, modules: modulesFor(defaultSetup(profile)) })).button.textContent, 'Setup saved');
    const changed = changeWiring(modulesFor(defaultSetup(profile)));
    const current = controls(mount({ ...options, modules: changed,
        verify: async () => { checks++; return false; } }));
    assert.equal(current.select.value, profile.name);
    assert.equal(current.button.textContent, 'Check setup');
    assert.equal(current.button.disabled, false);
    assert.notEqual(current.status.textContent, 'Product setup saved.');
    await current.button.fire('click');
    assert.equal(writes, 1, 'an external wiring change never causes an automatic rewrite');
    assert.equal(checks, 1);
    assert.equal(current.button.textContent, 'Check setup');
});
