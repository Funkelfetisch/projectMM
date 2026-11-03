import { test } from 'node:test';
import assert from 'node:assert/strict';
import { uploadThroughMoonbase } from '../../src/ui/helio-moonbase-upload.js';

const response = (ok, text = '') => ({ ok, status: ok ? 200 : 500, text: async () => text });
test('file upload waits for idle MoonBase after a lost reboot response', async () => {
    const calls = [], file = { size: 20 }; let clock = 0;
    const request = async (url, opts) => {
        calls.push([url, opts]);
        if (url.endsWith('/moonbase') && opts.method === 'POST') throw Error('reboot');
        if (url === '/moonbase') return response(true, 'idle');
        assert.equal(opts.body, file); return response(true, 'installed, restarting');
    };
    const result = await uploadThroughMoonbase(file, { request, now: () => clock, sleep: async ms => { clock += ms; } });
    assert.match(result, /accepted.*check the firmware version/);
    assert.deepEqual(calls.map(c => c[0]), ['/api/firmware/moonbase', '/moonbase', '/api/firmware/upload']);
    assert.ok(calls.every(c => c[1].redirect === 'error'));
});
test('redirected upload has an unknown outcome and is never retried', async () => {
    let uploads = 0, clock = 0;
    const result = await uploadThroughMoonbase({}, { now: () => clock, sleep: async ms => { clock += ms; },
        request: async (url, opts) => {
            assert.equal(opts.redirect, 'error');
            if (url === '/api/firmware/upload') { uploads++; throw new TypeError('redirect'); }
            return response(true, 'idle');
        } });
    assert.equal(uploads, 1); assert.match(result, /outcome unknown/);
});
test('busy MoonBase and rejected reboot never send the image', async () => {
    for (const rejected of [false, true]) {
        const calls = []; let clock = 0;
        const result = await uploadThroughMoonbase({}, { now: () => clock, sleep: async ms => { clock += ms; },
            request: async url => { calls.push(url); return url === '/moonbase' ? response(true, 'flashing') : response(!rejected); } });
        assert.match(result, rejected ? /rejected/ : /no image was uploaded/);
        assert.ok(!calls.includes('/api/firmware/upload'));
    }
});
test('lost upload response is ambiguous and never retried', async () => {
    let uploads = 0, clock = 0;
    const result = await uploadThroughMoonbase({}, { now: () => clock, sleep: async ms => { clock += ms; },
        request: async url => { if (url === '/api/firmware/upload') { uploads++; throw Error('lost'); } return response(true, 'idle'); } });
    assert.equal(uploads, 1); assert.match(result, /outcome unknown/);
});
test('a failed upload warns that the image may already be partially written', async () => {
    let uploads = 0, clock = 0;
    const result = await uploadThroughMoonbase({}, { now: () => clock, sleep: async ms => { clock += ms; },
        request: async url => {
            if (url === '/api/firmware/upload') { uploads++; return response(false, 'error: write failed'); }
            return response(true, 'idle');
        } });
    assert.equal(uploads, 1);
    assert.match(result, /Upload failed: error: write failed.*partially written.*before retrying/);
});
test('an unexpected successful upload status does not claim a committed image', async () => {
    let uploads = 0, clock = 0;
    const result = await uploadThroughMoonbase({}, { now: () => clock, sleep: async ms => { clock += ms; },
        request: async url => {
            if (url === '/api/firmware/upload') { uploads++; return { ok: true, status: 202 }; }
            return response(true, 'idle');
        } });
    assert.equal(uploads, 1); assert.match(result, /outcome unknown.*before retrying/);
});
test('an unrelated HTTP 200 page does not claim a committed image', async () => {
    let uploads = 0, clock = 0;
    const result = await uploadThroughMoonbase({}, { now: () => clock, sleep: async ms => { clock += ms; },
        request: async url => {
            if (url === '/api/firmware/upload') { uploads++; return response(true, '<html>login</html>'); }
            return response(true, 'idle');
        } });
    assert.equal(uploads, 1); assert.match(result, /outcome unknown.*before retrying/);
});
test('unreachable MoonBase reports recovery address and does not upload', async () => {
    let clock = 0, uploads = 0;
    const result = await uploadThroughMoonbase({}, { timeoutMs: 4000, now: () => clock, sleep: async ms => { clock += ms; },
        request: async url => { if (url === '/api/firmware/upload') uploads++; if (url === '/moonbase') throw Error('offline'); return response(true); } });
    assert.equal(uploads, 0); assert.match(result, /4\.3\.2\.1/);
});
test('a stalled MoonBase response body is aborted and never starts an upload', async () => {
    let clock = 0, uploads = 0, aborted = 0;
    const result = await uploadThroughMoonbase({}, { timeoutMs: 4000, probeTimeoutMs: 5,
        now: () => clock, sleep: async ms => { clock += ms; },
        request: async (url, opts) => {
            if (url === '/api/firmware/upload') uploads++;
            if (url !== '/moonbase') return response(true);
            return { ok: true, text: () => new Promise((_, reject) => {
                opts.signal.addEventListener('abort', () => { aborted++; reject(Error('aborted')); });
            }) };
        } });
    assert.equal(aborted, 1);
    assert.equal(uploads, 0);
    assert.match(result, /no image upload was started/);
});
test('an idle response arriving after the deadline cannot start an upload', async () => {
    let clock = 0, uploads = 0;
    const result = await uploadThroughMoonbase({}, { timeoutMs: 4000, now: () => clock,
        sleep: async ms => { clock += ms; }, request: async url => {
            if (url === '/api/firmware/upload') uploads++;
            if (url === '/moonbase') return { ok: true, text: async () => { clock = 4000; return 'idle'; } };
            return response(true);
        } });
    assert.equal(uploads, 0);
    assert.match(result, /no image upload was started/);
});


test('stalled upload or response body times out as unknown without retry', async () => {
    for (const stalledBody of [false, true]) {
        let uploads = 0, clock = 0, aborted = 0;
        const result = await uploadThroughMoonbase({}, { uploadTimeoutMs: 5,
            now: () => clock, sleep: async ms => { clock += ms; },
            request: async (url, opts) => {
                if (url !== '/api/firmware/upload') return response(true, 'idle');
                uploads++;
                const stall = () => new Promise((_, reject) => opts.signal.addEventListener('abort', () => {
                    aborted++; reject(Error('aborted'));
                }));
                return stalledBody ? { ok: true, status: 200, text: stall } : stall();
            } });
        assert.equal(uploads, 1);
        assert.equal(aborted, 1);
        assert.match(result, /outcome unknown.*before retrying/);
    }
});
