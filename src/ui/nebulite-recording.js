// Decode an explicitly dimensioned historical RGB recording. These are saved
// preview frames, not a live controller feed, animation algorithm or RGBW.
// The caller determines the recorded LED count; a product's present-day LED
// count must not be inferred from the byte length.
// SHA-256 for bounded, local migration data when Web Crypto is unavailable on
// a controller's plain-HTTP page. This digest detects mix-ups; it does not
// authenticate a preset or firmware. Algorithm: NIST FIPS 180-4 section 6.2.
export function recordingSha256(bytes) {
    if (!(bytes instanceof Uint8Array) || bytes.length > 1048576) throw new TypeError('Invalid recording bytes');
    const constants = [0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2];
    const padded = new Uint8Array(Math.ceil((bytes.length + 9) / 64) * 64);
    padded.set(bytes); padded[bytes.length] = 0x80;
    const view = new DataView(padded.buffer);
    view.setUint32(padded.length - 4, bytes.length * 8, false);
    const hash = [0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19];
    const words = new Uint32Array(64), rotate = (x,n) => (x >>> n) | (x << (32-n));
    for (let offset=0; offset<padded.length; offset+=64) {
        for (let i=0;i<16;i++) words[i]=view.getUint32(offset+4*i,false);
        for (let i=16;i<64;i++) {
            const x=words[i-15],y=words[i-2];
            words[i]=(words[i-16]+(rotate(x,7)^rotate(x,18)^(x>>>3))+words[i-7]+(rotate(y,17)^rotate(y,19)^(y>>>10)))>>>0;
        }
        let [a,b,c,d,e,f,g,h]=hash;
        for (let i=0;i<64;i++) {
            const t1=(h+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^(~e&g))+constants[i]+words[i])>>>0;
            const t2=((rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&c)^(b&c)))>>>0;
            h=g;g=f;f=e;e=(d+t1)>>>0;d=c;c=b;b=a;a=(t1+t2)>>>0;
        }
        [a,b,c,d,e,f,g,h].forEach((v,i)=>{hash[i]=(hash[i]+v)>>>0;});
    }
    return hash.map(value=>value.toString(16).padStart(8,'0')).join('');
}

export function decodeNebuliteRecording(encoded, ledCount, frameMs = 100) {
    if (typeof encoded !== 'string' || !encoded.length || encoded.length > 1048576 ||
        !Number.isInteger(ledCount) || ledCount < 1 || ledCount > 512 ||
        !Number.isInteger(frameMs) || frameMs < 1 || frameMs > 60000) return null;
    let binary;
    try { binary = atob(encoded); } catch { return null; }
    const stride = ledCount * 3;
    if (!binary.length || binary.length % stride || binary.length / stride > 1000) return null;
    const bytes = Uint8Array.from(binary, character => character.charCodeAt(0));
    return Object.freeze({ ledCount, frameMs, frameCount: bytes.length / stride,
        sample(frameIndex, ledIndex) {
            if (!Number.isInteger(frameIndex) || frameIndex < 0 || frameIndex >= bytes.length / stride ||
                !Number.isInteger(ledIndex) || ledIndex < 0 || ledIndex >= ledCount) return null;
            const p = frameIndex * stride + ledIndex * 3;
            return [bytes[p], bytes[p + 1], bytes[p + 2]];
        },
        frameAt(timeMs) {
            if (!Number.isFinite(timeMs) || timeMs < 0) return null;
            return Math.floor(timeMs / frameMs) % (bytes.length / stride);
        } });
}

// Pure preparation for a separately reviewed migration. It never writes a
// preset or applies a look; the caller must verify the source-to-look match.
export function attachRecordedPreview(document, encoded, ledCount, frameMs = 100) {
    if (!document || typeof document !== 'object' || Array.isArray(document) ||
        !document.Effects || typeof document.Effects !== 'object' || Array.isArray(document.Effects) ||
        !decodeNebuliteRecording(encoded, ledCount, frameMs)) return null;
    try {
        const copy = structuredClone(document);
        const forLook = JSON.stringify({ Effects: copy.Effects, Drivers: copy.Drivers ?? null });
        copy.$recordedPreview = { version: 1, encoding: 'rgb8-base64', ledCount, frameMs, data: encoded, forLook };
        return copy;
    } catch { return null; }
}

// Check the render parameters preserved in the source segment. Frame time and
// palette blend come from the separate archived configuration, not this segment.
function legacyControlsMatchSource(scene, segment, controls) {
    const byte = value => Number.isInteger(value) && value >= 0 && value <= 255;
    const color = index => {
        const value = segment.col?.[index];
        return Array.isArray(value) && (value.length === 3 || value.length === 4) && value.every(byte)
            ? [...value.slice(0, 3), value[3] ?? 0] : null;
    };
    const matchesColor = (source, names) => source && names.every((name, i) => controls[name] === source[i]);
    const primary = ['red', 'green', 'blue', 'white'];
    const secondary = ['secondary red', 'secondary green', 'secondary blue', 'secondary white'];
    if (scene === 'nebulite-solid') return controls.brightness === 255 && matchesColor(color(0), primary);
    if (!byte(segment.sx) || controls.speed !== segment.sx) return false;
    if (scene === 'nebulite-rainbow') return byte(segment.ix) && controls.size === segment.ix;
    if (['nebulite-blink', 'nebulite-washing', 'nebulite-sine', 'nebulite-firefly', 'nebulite-snowflakes', 'nebulite-flow-stripe', 'nebulite-plasmoid', 'nebulite-noisefire'].includes(scene) &&
        (!byte(segment.ix) || controls['effect intensity'] !== segment.ix)) return false;
    if (scene === 'nebulite-snowflakes') return controls['source brightness'] === 255 && controls['follow master'] === true && controls['frame ms'] === 24;
    if (scene === 'nebulite-flow-stripe') return segment.stop - segment.start >= 2;
    if (scene === 'nebulite-plasmoid') return controls['audio floor'] === 0 && controls['simulate if no source'] === true && matchesColor(color(1), secondary);
    if (scene === 'nebulite-noisefire') return controls['audio floor'] === 0 && controls['simulate if no source'] === true && segment.si === 0 && [0, 2].includes(segment.m12);
    if (scene === 'nebulite-washing') return true;
    if (scene === 'nebulite-sine' || scene === 'nebulite-firefly') return matchesColor(color(1), secondary);
    const first = color(0), second = color(1);
    if (!first || first[3] !== 0 || !matchesColor(first.slice(0, 3), primary.slice(0, 3))) return false;
    if (scene === 'nebulite-blink') return matchesColor(second, secondary);
    return scene === 'nebulite-palette' && second?.[3] === 0 &&
        matchesColor(second.slice(0, 3), secondary.slice(0, 3));
}

// Prepare one review candidate against a template from the exact target
// catalogue. The caller supplies archive bytes separately; no device I/O.
export async function prepareLegacyLook(template, candidate, catalog, encoded) {
    if (!template || !candidate || candidate.status !== 'review' ||
        !/^[0-9a-f]{12}$/.test(catalog) || template.$origin?.catalog !== catalog ||
        template.$origin?.scene !== candidate.scene ||
        !/^[0-9a-f]{64}$/.test(candidate.source_preset_sha256 || '') ||
        !/^[0-9a-f]{64}$/.test(candidate.source_recording_sha256 || '') ||
        typeof encoded !== 'string' || encoded.length > 1048576 ||
        !Array.isArray(candidate.source_segments) || candidate.source_segments.length !== 1 ||
        !candidate.controls || typeof candidate.controls !== 'object' || Array.isArray(candidate.controls)) return null;
    const segment = candidate.source_segments[0];
    if (candidate.source_is_matrix === true || (segment?.m12 === 2 && candidate.source_is_matrix !== false)) return null;
    if (!segment || !Number.isInteger(segment.start) || !Number.isInteger(segment.stop) ||
        segment.start < 0 || segment.stop <= segment.start || segment.stop - segment.start > 512) return null;
    const required = {
        'nebulite-solid': [0, null, ['red','green','blue','white','brightness','segment start','segment stop']],
        'nebulite-rainbow': [9, 11, ['speed','size','segment start','segment stop']],
        'nebulite-blink': [1, 3, ['speed','effect intensity','red','green','blue','secondary red','secondary green','secondary blue','secondary white','palette blend','frame ms','segment start','segment stop']],
        'nebulite-washing': [113, 1, ['speed','effect intensity','frame ms','segment start','segment stop']],
        'nebulite-sine': [108, 1, ['speed','effect intensity','secondary red','secondary green','secondary blue','secondary white','frame ms','segment start','segment stop']],
        'nebulite-firefly': [106, 35, ['speed','effect intensity','secondary red','secondary green','secondary blue','secondary white','frame ms','segment start','segment stop']],
        'nebulite-snowflakes': [74, 36, ['speed','effect intensity','source brightness','follow master','frame ms','segment start','segment stop']],
        'nebulite-flow-stripe': [179, 74, ['speed','effect intensity','frame ms','segment start','segment stop']],
        'nebulite-plasmoid': [133, 74, ['speed','effect intensity','audio floor','simulate if no source','secondary red','secondary green','secondary blue','secondary white','frame ms','segment start','segment stop']],
        'nebulite-noisefire': [143, null, ['speed','effect intensity','audio floor','simulate if no source','frame ms','segment start','segment stop']],
        'nebulite-palette': [65, 3, ['speed','red','green','blue','secondary red','secondary green','secondary blue','frame ms','segment start','segment stop']],
    }[candidate.scene];
    if (!required || segment.fx !== required[0] || (required[1] !== null && segment.pal !== required[1]) ||
        candidate.controls['segment start'] !== segment.start || candidate.controls['segment stop'] !== segment.stop ||
        Object.keys(candidate.controls).length !== required[2].length ||
        required[2].some(name => !Object.hasOwn(candidate.controls, name)) ||
        !legacyControlsMatchSource(candidate.scene, segment, candidate.controls)) return null;
    const schema = template.$uiSchema;
    if (!schema || !Array.isArray(schema.effectPath) || schema.effectPath.length < 2 ||
        schema.effectPath[0] !== 'Effects' || !schema.effectPath.every(p => typeof p === 'string') ||
        !Array.isArray(schema.controls)) return null;
    try {
        const bytes = Uint8Array.from(atob(encoded), character => character.charCodeAt(0));
        const hash = globalThis.crypto?.subtle
            ? Array.from(new Uint8Array(await globalThis.crypto.subtle.digest('SHA-256', bytes)), byte => byte.toString(16).padStart(2, '0')).join('')
            : recordingSha256(bytes);
        if (hash !== candidate.source_recording_sha256) return null;
        const document = structuredClone(template);
        let effect = document;
        for (const key of schema.effectPath) {
            if (key === '__proto__' || key === 'constructor' || key === 'prototype' ||
                !effect || !Object.hasOwn(effect, key)) return null;
            effect = effect[key];
        }
        if (!effect || typeof effect !== 'object' || effect.type !== schema.effectType) return null;
        for (const [name, value] of Object.entries(candidate.controls)) {
            const descriptors = schema.controls.filter(c => c?.name === name && !c.path);
            if (descriptors.length !== 1 || !Object.hasOwn(effect, name)) return null;
            const descriptor = descriptors[0];
            if (descriptor.type === 'bool' ? typeof value !== 'boolean' :
                !Number.isInteger(value) || !Number.isFinite(descriptor.min) ||
                !Number.isFinite(descriptor.max) || value < descriptor.min || value > descriptor.max) return null;
            effect[name] = value;
        }
        const result = attachRecordedPreview(document, encoded, segment.stop - segment.start, 100);
        if (!result) return null;
        result.$recordedPreview.sourcePresetSha256 = candidate.source_preset_sha256;
        result.$recordedPreview.sourceRecordingSha256 = hash;
        result.$recordedPreview.qualification = 'historical-preview-unverified';
        if (new TextEncoder().encode(JSON.stringify(result)).length > 32768) return null;
        return result;
    } catch { return null; }
}

// Only use a recording while the current look matches the stored source look.
// A metadata edit or a saved animation edit must invalidate the cached frame.
const presetCache = new WeakMap();
export function recordingForPreset(document, cacheOwner = document) {
    if (!document || typeof document !== 'object' || Array.isArray(document)) return null;
    const meta = document.$recordedPreview;
    if (!meta || meta.version !== 1 || meta.encoding !== 'rgb8-base64' ||
        typeof meta.forLook !== 'string' || typeof meta.data !== 'string') return null;
    let current;
    try { current = JSON.stringify({ Effects: document.Effects ?? null, Drivers: document.Drivers ?? null }); }
    catch { return null; }
    if (current !== meta.forLook) return null;
    if (!cacheOwner || typeof cacheOwner !== 'object') return null;
    const cached = presetCache.get(cacheOwner);
    if (cached && cached.data === meta.data && cached.ledCount === meta.ledCount && cached.frameMs === meta.frameMs)
        return cached.recording;
    const recording = decodeNebuliteRecording(meta.data, meta.ledCount, meta.frameMs);
    presetCache.set(cacheOwner, { data: meta.data, ledCount: meta.ledCount, frameMs: meta.frameMs, recording });
    return recording;
}
