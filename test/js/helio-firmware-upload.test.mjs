import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

test('firmware upload distinguishes acceptance, rejection and a lost response', async () => {
    const source = readFileSync(new URL('../../src/ui/helio-app.js', import.meta.url), 'utf8');
    const start = source.indexOf('upInput.addEventListener("change", async () => {');
    const end = source.indexOf('\n        upBtn.addEventListener("click"', start);
    assert.ok(start >= 0 && end > start);
    let handler;
    let reply;
    const file = { size: 336 };
    const upInput = { files: [file], value: 'firmware.bin', addEventListener(_event, cb) { handler = cb; } };
    const upBtn = { disabled: false };
    const upStatus = { textContent: '' };
    const context = vm.createContext({ upInput, upBtn, upStatus, mod: { controls: [] }, fmSize: String, AbortController, setTimeout, clearTimeout,
        errorMessage: async () => 'wrong image',
        fetch: async (url, options) => {
            assert.equal(url, '/api/firmware/upload');
            assert.equal(options.body, file);
            if (reply instanceof Error) throw reply;
            return reply;
        } });
    vm.runInContext(source.slice(start, end), context);

    reply = { ok: true, status: 200, json: async () => ({ ok: true }) };
    await handler();
    assert.match(upStatus.textContent, /update accepted.*check the firmware version/);
    reply = { ok: true, status: 202 };
    await handler();
    assert.match(upStatus.textContent, /outcome unknown.*before retrying/);
    reply = { ok: true, status: 200, json: async () => { throw new SyntaxError('not JSON'); } };
    await handler();
    assert.match(upStatus.textContent, /outcome unknown/);
    reply = { ok: true, status: 200, json: async () => ({ ok: false }) };
    await handler();
    assert.match(upStatus.textContent, /outcome unknown/);
    assert.equal(upBtn.disabled, false);
    assert.equal(upInput.value, '');

    reply = { ok: false };
    await handler();
    assert.match(upStatus.textContent, /upload failed: wrong image.*partially written.*before retrying/);
    assert.equal(upBtn.disabled, false);

    reply = new Error('connection closed');
    await handler();
    assert.match(upStatus.textContent, /outcome unknown.*check the firmware version/);
    assert.equal(upBtn.disabled, false);
});

test('release installation reports an ambiguous lost response separately from rejection', async () => {
    const source = readFileSync(new URL('../../src/ui/helio-app.js', import.meta.url), 'utf8');
    const marker = 'onInstall: async (_firmware, _manifestUrl, binaryUrl) => {';
    const start = source.indexOf(marker);
    const end = source.indexOf('\n                },', start);
    assert.ok(start >= 0 && end > start);
    let reply;
    const context = vm.createContext({
        errorMessage: async () => 'wrong image',
        fetch: async (url, options) => {
            assert.equal(url, '/api/firmware/url');
            assert.equal(JSON.parse(options.body).url, 'https://example.test/image.bin');
            if (reply instanceof Error) throw reply;
            return reply;
        },
    });
    const callback = vm.runInContext('(' + source.slice(start + 'onInstall: '.length, end) + '\n})', context);
    const install = () => callback(null, null, 'https://example.test/image.bin');
    reply = { ok: true };
    await install();
    reply = { ok: false };
    await assert.rejects(install(), /wrong image/);
    reply = new Error('connection closed');
    await assert.rejects(install(), /outcome unknown.*check the firmware version before retrying/);
});


test('MoonBase devices hand the selected file to the reboot-aware uploader', async () => {
    const source = readFileSync(new URL('../../src/ui/helio-app.js', import.meta.url), 'utf8');
    const start = source.indexOf('upInput.addEventListener("change", async () => {');
    const end = source.indexOf('\n        upBtn.addEventListener("click"', start);
    let handler, sent; const file = { size: 10 };
    const upInput = { files: [file], value: '', addEventListener(_event, cb) { handler = cb; } };
    const upBtn = { disabled: false }, upStatus = { textContent: '' };
    const context = vm.createContext({ upInput, upBtn, upStatus, mod: { controls: [{ name: 'image' }] }, fmSize: String,
        uploadThroughMoonbase: async (value, options) => { sent = value; options.status('waiting'); return 'accepted'; },
        fetch: async () => { throw Error('direct upload must not be called'); } });
    vm.runInContext(source.slice(start, end), context);
    await handler(); assert.equal(sent, file); assert.equal(upStatus.textContent, 'accepted'); assert.equal(upBtn.disabled, false);
});

test('direct upload times out both transfer and response body without retry', async () => {
    const source = readFileSync(new URL('../../src/ui/helio-app.js', import.meta.url), 'utf8');
    const start = source.indexOf('upInput.addEventListener("change", async () => {');
    const end = source.indexOf('\n        upBtn.addEventListener("click"', start);
    for (const stalledBody of [false, true]) {
        let handler, expire, uploads = 0, clears = 0;
        const upInput = { files: [{ size: 336 }], value: 'firmware.bin', addEventListener(_event, cb) { handler = cb; } };
        const upBtn = { disabled: false }, upStatus = { textContent: '' };
        const context = vm.createContext({ upInput, upBtn, upStatus, mod: { controls: [] }, fmSize: String,
            AbortController, setTimeout: fn => { expire = fn; return 1; }, clearTimeout: () => { clears++; },
            fetch: async (_url, options) => {
                uploads++;
                const stall = () => new Promise((_, reject) => {
                    if (options.signal.aborted) reject(Error('aborted'));
                    else options.signal.addEventListener('abort', () => reject(Error('aborted')));
                });
                return stalledBody ? { ok: true, status: 200, json: stall } : stall();
            } });
        vm.runInContext(source.slice(start, end), context);
        const pending = handler();
        await Promise.resolve();
        expire();
        await pending;
        assert.equal(uploads, 1);
        assert.equal(clears, 1);
        assert.equal(upBtn.disabled, false);
        assert.match(upStatus.textContent, /outcome unknown.*check the firmware version/);
    }
});
