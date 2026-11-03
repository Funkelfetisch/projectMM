// Carry one user-selected image across the app -> MoonBase reboot. A lost
// response is ambiguous, so never automatically restart an image transfer.
export async function uploadThroughMoonbase(file, { request = fetch, now = () => performance.now(),
    sleep = ms => new Promise(resolve => setTimeout(resolve, ms)), status = () => {},
    timeoutMs = 120000, probeTimeoutMs = 3000, uploadTimeoutMs = 180000 } = {}) {
    status('Restarting into MoonBase…');
    let kickoff;
    const kickoffController = new AbortController();
    const kickoffTimer = setTimeout(() => kickoffController.abort(), 5000);
    try { kickoff = await request('/api/firmware/moonbase', { method: 'POST', redirect: 'error', signal: kickoffController.signal }); }
    catch (_) { /* The reboot may have interrupted the response. Probe before acting. */ }
    finally { clearTimeout(kickoffTimer); }
    if (kickoff && !kickoff.ok) return 'MoonBase restart rejected; no image was uploaded';
    const deadline = now() + timeoutMs;
    while (now() < deadline) {
        await sleep(Math.min(2000, Math.max(0, deadline - now())));
        if (now() >= deadline) break;
        let state;
        try {
            const controller = new AbortController();
            const timer = setTimeout(() => controller.abort(), Math.min(probeTimeoutMs, Math.max(0, deadline - now())));
            try {
                const probe = await request('/moonbase', { cache: 'no-store', redirect: 'error', signal: controller.signal });
                if (!probe.ok) continue;
                state = (await probe.text()).trim();
            }
            finally { clearTimeout(timer); }
        } catch (_) { continue; }
        if (now() >= deadline) break;
        if (state !== 'idle') return `MoonBase is ${state || 'busy'}; no image was uploaded. Open its page to inspect the device.`;
        status('Uploading to MoonBase…');
        const uploadController = new AbortController();
        const uploadTimer = setTimeout(() => uploadController.abort(), uploadTimeoutMs);
        try {
            const response = await request('/api/firmware/upload', {
                method: 'POST', redirect: 'error', signal: uploadController.signal, headers: { 'Content-Type': 'application/octet-stream' }, body: file,
            });
            if (!response.ok) return `Upload failed: ${await response.text()}. The image may be partially written; reconnect and inspect the device before retrying.`;
            if (response.status !== 200) return 'Upload outcome unknown; reconnect and check the firmware version before retrying';
            if ((await response.text()).trim() !== 'installed, restarting')
                return 'Upload outcome unknown; reconnect and check the firmware version before retrying';
            return 'Update accepted — device restarting; reconnect and check the firmware version';
        } catch (_) {
            return 'Connection lost or upload timed out — update outcome unknown; reconnect and check the firmware version before retrying';
        } finally { clearTimeout(uploadTimer); }
    }
    return 'MoonBase did not answer. Reconnect to the device or its MoonBase access point and open http://4.3.2.1; no image upload was started.';
}
