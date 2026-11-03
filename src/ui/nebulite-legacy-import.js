import { prepareLegacyLook } from './nebulite-recording.js';

// This accepts a reviewed extraction, not an arbitrary ZIP or a firmware image.
// Preparation is read-only; storage happens only through the explicit commit.
export async function prepareLegacyImport(text, expectedProduct, client) {
    if (typeof text !== 'string' || text.length > 1048576) throw new Error('Import file is too large or invalid');
    let bundle;
    try { bundle = JSON.parse(text); } catch { throw new Error('Import file is not valid JSON'); }
    if (bundle?.format !== 'nebulite-wled-review-v1' || !['jacket','croptop'].includes(expectedProduct) ||
        bundle.product !== expectedProduct || !/^[0-9a-f]{12}$/.test(bundle.catalog || '') ||
        !Array.isArray(bundle.items) || bundle.items.length > 64) throw new Error('Import file does not match this product');
    // The embedded controller has limited simultaneous HTTP/JSON buffers.
    // Finish each read before starting the next; conditional create still
    // protects against changes after this snapshot.
    const catalog = await client.listTemplates();
    if (catalog.catalog !== bundle.catalog) throw new Error('The controller animation catalogue does not match this file');
    const existing = await client.list();
    const names = new Set((existing.items || []).map(item => item.name));
    const templates = new Map(), ready = [], skipped = [];
    for (const item of bundle.items) {
        const candidate = item?.candidate, sourceName = candidate?.name;
        if (candidate?.status !== 'review') { skipped.push({ name: typeof sourceName === 'string' ? sourceName : 'Unnamed', reason: 'Not supported yet' }); continue; }
        // The firmware's filename format is ASCII-only. Retain familiar music
        // looks under a safe, explicit name rather than rejecting them outright.
        const portableName = typeof sourceName === 'string' ? sourceName.replace(/^[♪♫]\s*/, 'Music - ') : '';
        if (!/^[\x20-\x7e]{1,31}$/.test(portableName) || /[./\\]/.test(portableName)) {
            skipped.push({ name: typeof sourceName === 'string' ? sourceName : 'Unnamed', reason: 'Name needs a printable, unique name' }); continue;
        }
        // Preserve a same-named factory or customer preset while making the
        // historical candidate available for explicit review and creation.
        const name = names.has(portableName) ? portableName.slice(0, 24) + ' (WLED)' : portableName;
        if (names.has(name)) { skipped.push({ name, reason: 'A preset with this name already exists' }); continue; }
        let template = templates.get(candidate.scene);
        if (!template) { template = (await client.readTemplate(candidate.scene)).document; templates.set(candidate.scene, template); }
        const document = await prepareLegacyLook(template, candidate, bundle.catalog, item.recording);
        if (!document) { skipped.push({ name, reason: 'Recording or settings could not be verified' }); continue; }
        delete document.$slot;
        const warnings = candidate.scene === 'nebulite-noisefire' && candidate.source_segments[0]?.m12 === 2
            ? ['The source selects 1D-to-2D arc mapping; it is inactive for its configured 1D strip.'] : [];
        if (candidate.scene === 'nebulite-noisefire' || candidate.scene === 'nebulite-plasmoid')
            warnings.push('Audio levels are not calibrated to the original device; if no audio service is active, the historical beat is simulated.');
        ready.push({ name, sourceName, document, warnings }); names.add(name);
    }
    const limit = Number.isInteger(existing.limit) ? existing.limit : 64;
    if ((existing.items || []).length + ready.length > limit) throw new Error('There is not enough preset storage for this import');
    return { ready, skipped };
}

export async function commitLegacyImport(review, client, onProgress = () => {}) {
    const saved = [];
    for (const item of review.ready) {
        onProgress(saved.length, review.ready.length, item.name);
        try { await client.write(item.name, item.document, { create: true }); }
        catch (error) { return { saved, failed: item.name, error }; }
        saved.push(item.name);
    }
    onProgress(saved.length, review.ready.length, null);
    return { saved, failed: null, error: null };
}
