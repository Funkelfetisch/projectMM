import {
    HELIO_DEFAULT_LED_COUNT,
    HELIO_SCENE_PLAYLISTS,
    HELIO_SCENE_PRESETS,
    helioFactorySceneForType,
} from "./helio-scenes.js";

const DEFAULT_HELIO_PREVIEW_PALETTE = [
    [12, 18, 30],
    [255, 178, 74],
    [255, 236, 188],
    [70, 160, 255],
    [111, 255, 0],
];

const HELIO_STATIC_SCENE_ORDER = HELIO_SCENE_PLAYLISTS[0]?.order || [7];
const HELIO_AUDIO_SCENE_ORDER = HELIO_SCENE_PLAYLISTS[1]?.order || [16];
const HELIO_PORT_SCENE_COUNT = 19;

export const HELIO_PREVIEW_AUDIO = Object.freeze({
    level: 0.72,
    bands: Object.freeze(Array.from({ length: 16 }, (_value, i) => Math.max(0.2, 0.92 - i * 0.035))),
});

function controlList(controls) {
    if (Array.isArray(controls)) return controls;
    if (Array.isArray(controls?.controls)) return controls.controls;
    return Object.entries(controls || {}).map(([name, value]) => ({ name, value }));
}

export function controlValue(mod, name, fallback = 0) {
    const c = controlList(mod).find(x => x.name === name);
    return c && c.value !== undefined ? c.value : fallback;
}

export function clampByte(v) {
    return Math.max(0, Math.min(255, Math.round(Number(v) || 0)));
}

// A linear effect preview, not a reconstruction of the garment's LED geometry.
export function drawNebuliteStripPreview(canvas, time, sampler, count) {
    const context = canvas?.getContext?.("2d");
    const width = canvas?.width || 0, height = canvas?.height || 0;
    if (!context || !width || !height || typeof sampler !== "function") return false;
    const lights = Math.max(1, Math.min(512, Math.round(Number(count) || 1)));
    context.fillStyle = "#05070d";
    context.fillRect(0, 0, width, height);
    const left = Math.round(width * 0.08), top = Math.round(height * 0.38);
    const stripWidth = Math.round(width * 0.84), stripHeight = Math.round(height * 0.24);
    for (let i = 0; i < lights; i++) {
        const color = sampler((i + 0.5) / lights, 0.5, time) || [];
        context.fillStyle = `rgb(${clampByte(color[0])}, ${clampByte(color[1])}, ${clampByte(color[2])})`;
        const x = left + Math.round(stripWidth * i / lights);
        const next = left + Math.round(stripWidth * (i + 1) / lights);
        // A one-pixel gap where each light is wide enough, so a solid colour still reads as separate LEDs.
        if (next > x) context.fillRect(x, top, next - x > 3 ? next - x - 1 : next - x, stripHeight);
    }
    canvas.dataset.previewRenderer = "nebulite-strip-simulation";
    canvas.setAttribute("aria-label", "Approximate animation preview on a linear RGB LED strip");
    return true;
}

export function scaleRgb(c, scale) {
    return [clampByte(c[0] * scale), clampByte(c[1] * scale), clampByte(c[2] * scale)];
}

export function mix(a, b, t) {
    return [
        clampByte(a[0] + (b[0] - a[0]) * t),
        clampByte(a[1] + (b[1] - a[1]) * t),
        clampByte(a[2] + (b[2] - a[2]) * t),
    ];
}

export function rgbwToRgb(c) {
    const r = clampByte(c?.[0]);
    const g = clampByte(c?.[1]);
    const b = clampByte(c?.[2]);
    const w = clampByte(c?.[3]);
    return [
        clampByte(r + w),
        clampByte(g + w * 0.69),
        clampByte(b + w * 0.43),
    ];
}

export function rgbToRgbw(c) {
    const r = clampByte(c?.[0]);
    const g = clampByte(c?.[1]);
    const b = clampByte(c?.[2]);
    const white = Math.max(0, Math.min(r, g / 0.69, b / 0.43));
    return [
        clampByte(r - white),
        clampByte(g - white * 0.69),
        clampByte(b - white * 0.43),
        clampByte(white),
    ];
}

export function paletteAt(pal, x) {
    const colors = Array.isArray(pal) && pal.length ? pal : DEFAULT_HELIO_PREVIEW_PALETTE;
    if (colors.length === 1) return colors[0];
    const wheel = ((Math.floor((Number(x) || 0) * 255 + 1e-9) % 256) + 256) % 256;
    const position = colors.length === 16 ? wheel : Math.floor(wheel * colors.length / 16);
    const index = position >> 4;
    const amount = (position & 0x0f) * 17;
    const next = (index + 1) % colors.length;
    const inverse = 255 - amount;
    return [0, 1, 2].map(channel => clampByte(
        Math.floor(clampByte(colors[index]?.[channel]) * inverse / 255) +
        Math.floor(clampByte(colors[next]?.[channel]) * amount / 255)
    ));
}

export function fract(v) {
    return ((v % 1) + 1) % 1;
}

export function ringDist(a, b) {
    const d = Math.abs(fract(a) - fract(b));
    return Math.min(d, 1 - d);
}

export function hash01(a, b) {
    let x = (Math.imul(Math.floor(a) | 0, 0x45d9f3b) + Math.imul(Math.floor(b) | 0, 0x27d4eb2d) + 0x165667b1) >>> 0;
    x ^= x >>> 16;
    x = Math.imul(x, 0x7feb352d) >>> 0;
    x ^= x >>> 15;
    return ((x >>> 24) & 255) / 255;
}

const BYTE_SIN_LUT = Uint8Array.from({ length: 256 }, (_value, i) =>
    Math.round(Math.sin(i * Math.PI / 128) * 127 + 128));

function byteIndex(value) {
    return ((Math.floor(Number(value) || 0) % 256) + 256) % 256;
}

function byteSin(phase) {
    return BYTE_SIN_LUT[byteIndex(phase)];
}

function byteCos(phase) {
    return byteSin((Number(phase) || 0) + 64);
}

function byteBeat(bpm, t) {
    const beatsPerMinute = clampByte(bpm);
    if (beatsPerMinute === 0) return 0;
    const periodMs = Math.floor(60000 / beatsPerMinute);
    if (periodMs === 0) return 0;
    const nowMs = Math.floor(Math.max(0, Number(t) || 0) * 1000) >>> 0;
    return Math.floor((nowMs % periodMs) * 256 / periodMs) & 255;
}

function signedBeat(bpm, t, low, high) {
    return Math.trunc(low + byteSin(byteBeat(bpm, t)) * (high - low) / 255);
}

function triWave8(phase) {
    const value = byteIndex(phase);
    return value < 128 ? value * 2 : (255 - value) * 2;
}

function cubicWave8(phase) {
    const triangle = triWave8(phase);
    const square = Math.floor(triangle * triangle / 255);
    const cube = Math.floor(square * triangle / 255);
    return Math.max(0, Math.min(255, 3 * square - 2 * cube));
}

function hsvToRgbPreview(h, s, v) {
    const hue = (((Number(h) || 0) % 256) + 256) % 256 / 256 * 6;
    const saturation = Math.max(0, Math.min(1, (Number(s) || 0) / 255));
    const value = Math.max(0, Math.min(1, (Number(v) || 0) / 255));
    const sector = Math.floor(hue) % 6;
    const fraction = hue - Math.floor(hue);
    const p = value * (1 - saturation);
    const q = value * (1 - saturation * fraction);
    const r = value * (1 - saturation * (1 - fraction));
    const table = [[value, r, p], [q, value, p], [p, value, r], [p, q, value], [r, p, value], [value, p, q]];
    return table[sector].map(channel => clampByte(channel * 255));
}

function addRgb(a, b) {
    return [clampByte(a[0] + b[0]), clampByte(a[1] + b[1]), clampByte(a[2] + b[2])];
}

export function audioBand(audio, band) {
    const idx = Math.max(0, Math.min(15, Math.floor(Number(band) || 0)));
    return Math.max(0, Math.min(1, Number(audio?.bands?.[idx]) || 0));
}

export function strongestBand(audio) {
    let best = 0, mag = -1;
    for (let i = 0; i < 16; i++) {
        const v = audioBand(audio, i);
        if (v > mag) { mag = v; best = i; }
    }
    return best;
}

export function helioScenePresetIndex(effect, t = 0) {
    const scene = helioFactorySceneForType(effect?.type);
    if (scene) return scene.presetIndex;

    const playlist = Number(controlValue(effect, "playlist", 1)) || 0;
    const autoPlay = controlValue(effect, "autoPlay", true);
    if (!autoPlay) return Math.max(0, Math.min(HELIO_PORT_SCENE_COUNT - 1, Number(controlValue(effect, "preset", 16)) || 0));

    const order = playlist === 0 ? HELIO_STATIC_SCENE_ORDER : HELIO_AUDIO_SCENE_ORDER;
    const dur = playlist === 0 ? 10 : 30;
    return order[Math.floor(Math.max(0, t) / dur) % order.length];
}

export function helioPresetPreviewColor(presetIndex, controls = {}, pal = DEFAULT_HELIO_PREVIEW_PALETTE, t = 0, x = 0, y = 0.5, audio = HELIO_PREVIEW_AUDIO, opts = {}) {
    const preset = Math.max(0, Math.min(HELIO_SCENE_PRESETS.length - 1, Number(presetIndex) || 0));
    const cfg = HELIO_SCENE_PRESETS[preset] || HELIO_SCENE_PRESETS[0];
    const effect = {
        type: controls?.type || cfg.type,
        controls: controlList(controls),
    };
    const paletteForIndex = typeof opts.paletteByIndex === "function"
        ? opts.paletteByIndex
        : (_index, fallback) => fallback;
    const presetPal = paletteForIndex(controlValue(effect, "palette", cfg.palette), pal);
    const speed = controlValue(effect, "speed", null);
    const bpm = controlValue(effect, "bpm", null);
    const speedScale = Math.max(0.05, speed === null
        ? (bpm === null ? (Number(cfg.speed) || 128) / 128 : (Number(bpm) || 0) / 60)
        : (Number(speed) || 0) / 128);
    const speedByte = clampByte(speed === null ? cfg.speed : speed);
    const pt = t * speedScale;
    const u = fract(x + y * 0.08);
    const stripU = fract(x);
    const effectByte = clampByte(controlValue(effect, "effect intensity", cfg.intensity));
    const effectAmount = effectByte / 255;
    const output = clampByte(controlValue(effect, "output", 255)) / 255;
    const ledCount = Math.max(1, Math.round(Number(opts.ledCount) || HELIO_DEFAULT_LED_COUNT));
    const white = clampByte(controlValue(effect, "white", cfg.white || 0));
    const primaryRgb = [
        clampByte(controlValue(effect, "red", cfg.primary?.[0] ?? 255)),
        clampByte(controlValue(effect, "green", cfg.primary?.[1] ?? 133)),
        clampByte(controlValue(effect, "blue", cfg.primary?.[2] ?? 0)),
    ];
    const secondaryRgb = [
        clampByte(controlValue(effect, "secondary red", cfg.secondary?.[0] ?? 0)),
        clampByte(controlValue(effect, "secondary green", cfg.secondary?.[1] ?? 0)),
        clampByte(controlValue(effect, "secondary blue", cfg.secondary?.[2] ?? 0)),
    ];
    const level = Math.max(0, Math.min(1, Number(audio?.level) || 0));
    const scaledRgbw = (rgb, whiteValue = 0, brightness = 1) => {
        const amount = output * Math.max(0, Math.min(1, brightness));
        const logical = [
            clampByte(rgb[0] * amount),
            clampByte(rgb[1] * amount),
            clampByte(rgb[2] * amount),
        ];
        // The NEBULITE garment profiles have RGB strips and a separate PWM
        // white output; mixing that white into every RGB pixel is misleading.
        if (opts.rgbOnly) return logical;
        const explicitWhite = clampByte(whiteValue * amount);
        const derivedWhite = explicitWhite || Math.min(logical[0], logical[1], logical[2]);
        return rgbwToRgb([...logical, derivedWhite]);
    };
    const scaled = (c, s = 1) => scaledRgbw(c, 0, s);
    const palc = (p, s = 1) => scaled(paletteAt(presetPal, p), s);
    const nowMs = Math.max(0, Number(t) || 0) * 1000;
    const ledIndex = Math.min(ledCount - 1, Math.floor(stripU * ledCount));
    const stripPos = ledCount > 1 ? ledIndex / (ledCount - 1) : 0;
    const sourcePalette = Number(cfg.source?.palette) || 0;
    const scenePaletteIndex = clampByte(controlValue(effect, "palette", cfg.palette));
    const sourceUsesPrimary = sourcePalette === 0 && scenePaletteIndex === 0;
    const paletteByte = (index, brightness = 1) => palc((((Number(index) || 0) % 256) + 256) % 256 / 255, brightness);
    const audioLevel = Math.max(0, Math.min(1, level));
    const peakBand = strongestBand(audio);
    const peakPosition = peakBand / 15;
    const freqMapTrail = reactive => {
        const historyCount = reactive ? 30 : 56;
        const decay = reactive ? 0.945 : 0.978;
        const width = 1.4 / ledCount;
        const bpmValue = 3 + speedByte * 8 / 255;
        let color = [0, 0, 0];
        for (let history = 0; history < historyCount; history++) {
            const historyTime = t - history / 35;
            const center = reactive
                ? Math.max(0, Math.min(1, 0.08 + peakPosition * 0.84 + Math.sin(historyTime * 2.1 + peakBand) * 0.07))
                : 0.5 + Math.sin(historyTime * bpmValue / 60 * Math.PI * 2) * 0.5;
            const footprint = Math.max(0, 1 - Math.abs(stripPos - center) / width);
            if (footprint <= 0) continue;
            const brightness = footprint * Math.pow(decay, history) * (reactive ? Math.max(0.2, audioLevel) : 0.86);
            color = addRgb(color, paletteByte(effectByte + center * 255 + history * (reactive ? 5 : 2), brightness));
        }
        return color;
    };

    if (preset === 0) {
        const frame = nowMs / 25;
        const bucket = Math.floor(frame / 4);
        const blendFrame = fract(frame / 4);
        const target = sample => {
            const random = (hash01(ledIndex, sample * 2) + hash01(ledIndex, sample * 2 + 1)) * 0.5;
            const floor = (255 - effectByte) / 255;
            return floor + (1 - floor) * (0.25 + random * 0.75);
        };
        const brightness = target(bucket) + (target(bucket + 1) - target(bucket)) * blendFrame;
        return scaledRgbw(primaryRgb, white, brightness);
    }
    if (preset === 1) {
        const lifetime = Math.max(0.42, 1.15 - speedByte / 420);
        const local = (t + hash01(ledIndex, 3) * lifetime) / lifetime;
        const cycle = Math.floor(local);
        const life = fract(local);
        if (hash01(ledIndex, cycle) > 0.08 + effectAmount * 0.3) return [0, 0, 0];
        const brightness = life < 0.34
            ? cubicWave8((life / 0.34) * 64) / 255
            : Math.pow(1 - (life - 0.34) / 0.66, 1.6);
        return paletteByte(hash01(ledIndex, cycle + 71) * 255, brightness);
    }
    if (preset === 2) {
        return freqMapTrail(false);
    }
    if (preset === 15) {
        return freqMapTrail(true);
    }
    if (preset === 3) {
        const lakeSpeed = Math.floor(speedByte / 10);
        const wave1 = signedBeat(lakeSpeed + 2, t, -64, 64);
        const wave2 = signedBeat(lakeSpeed + 1, t, -64, 64);
        const wave3 = signedBeat(lakeSpeed + 2, t, 0, 80);
        const index = (Math.floor(byteCos(ledIndex * 15 + wave1) / 2) +
            Math.floor(cubicWave8(ledIndex * 23 + wave2) / 2)) & 255;
        const luminance = Math.max(0, index - wave3) / 255;
        return sourceUsesPrimary
            ? scaledRgbw(primaryRgb, white, luminance)
            : paletteByte(index, luminance);
    }
    if (preset === 4) {
        const counter = (Math.floor(nowMs * ((speedByte >> 2) + 1)) & 0xffff) >>> 0;
        const head = Math.floor(counter * ledCount / 65536);
        const distance = ledIndex > head ? head + ledCount - ledIndex : head - ledIndex;
        const amount = distance < Math.max(1, effectByte) ? distance / Math.max(1, effectByte) : 1;
        return scaledRgbw(mix(primaryRgb, paletteAt(presetPal, stripPos), amount), white * (1 - amount));
    }
    if (preset === 5) {
        const counter = speedByte === 0 ? 0 : ((Math.floor(nowMs * ((speedByte >> 3) + 1)) & 0xffff) >> 8);
        return paletteByte((ledIndex * 255 / ledCount) - counter);
    }
    if (preset === 6) {
        const framePhase = t * 35 * speedByte / 32;
        const wave = cubicWave8((ledIndex + 1) * 16 + framePhase * (ledIndex % 5 + 1) / 2);
        const amount = Math.max(0, wave - (255 - effectByte)) / 255;
        const indexStep = Math.floor(256 / ledCount) + (ledCount > 256 ? 1 : 0);
        return scaled(mix(secondaryRgb, paletteAt(presetPal, fract((nowMs / 64 + ledIndex * indexStep) / 255)), amount));
    }
    if (preset === 7) {
        const count = Math.max(1, Math.min(8, clampByte(controlValue(effect, "count", 4))));
        const ringSpeed = clampByte(controlValue(effect, "speed", 42));
        const thickness = Math.max(0.012, clampByte(controlValue(effect, "thickness", 5)) / 50);
        const hueShift = clampByte(controlValue(effect, "hue_shift", 0));
        let color = paletteByte(hueShift, clampByte(controlValue(effect, "background brightness", 0)) / 255);
        for (let ring = 0; ring < count; ring++) {
            const age = fract(t * Math.max(0.04, ringSpeed / 255 * 0.55) + ring / count);
            const radius = age * 1.15;
            const centerX = 0.12 + hash01(ring, 17) * 0.76;
            const centerY = 0.12 + hash01(ring, 29) * 0.76;
            const distance = Math.hypot(x - centerX, y - centerY);
            const profile = Math.max(0, 1 - Math.abs(distance - radius) / thickness);
            const brightness = profile * (1 - age * 0.82);
            color = addRgb(color, paletteByte(hash01(ring, 43) * 255 + hueShift, brightness));
        }
        return color;
    }
    if (preset === 8) {
        const counter = Math.floor(nowMs * speedByte / 512) >>> 0;
        const amount = byteSin(ledIndex * (effectByte >> 2) - counter) / 255;
        const sceneColor = sourceUsesPrimary ? primaryRgb : paletteAt(presetPal, stripPos);
        return scaledRgbw(mix(secondaryRgb, sceneColor, amount), sourceUsesPrimary ? white * amount : 0);
    }
    if (preset === 9) {
        let color = [0, 0, 0];
        for (let star = 0; star < 7; star++) {
            const cadence = 2.1 + star * 0.07;
            const local = (t + hash01(star, 8) * cadence) / cadence;
            const cycle = Math.floor(local);
            const age = fract(local) * cadence;
            if (age > 1.75) continue;
            const origin = hash01(star, cycle + 19);
            const velocity = (0.22 + hash01(star, cycle + 23) * 0.95) / Math.max(1, ledCount / 62);
            const fade = age < 0.25 ? 0 : Math.min(1, (age - 0.25) / 1.5);
            const ignition = age < 0.25 ? age / 0.25 : 1;
            const starColor = mix([255, 255, 255], hsvToRgbPreview(hash01(star, cycle + 31) * 255, 255, 255), ignition);
            const radius = Math.max(0.5 / ledCount, (1 - fade) * 2 / ledCount);
            for (let fragment = 0; fragment < 9; fragment++) {
                const velocityBand = Math.floor(fragment / 2);
                const travel = velocity * velocityBand * (1 - Math.exp(-3 * age)) / 9;
                for (const direction of [-1, 1]) {
                    const position = origin + direction * travel;
                    const footprint = Math.max(0, 1 - Math.abs(stripPos - position) / radius);
                    if (footprint > 0) color = addRgb(color, scaled(starColor, footprint * (1 - fade) * (0.45 + audioLevel * 0.8)));
                }
            }
        }
        return color;
    }
    if (preset === 10) {
        const reach = Math.min(0.5, audioLevel * effectByte / 256);
        const distance = Math.abs(stripPos - 0.5);
        if (Math.abs(distance - reach) <= 1.2 / ledCount) return paletteByte(nowMs);
        if (distance >= reach || reach <= 0) return [0, 0, 0];
        const noise = hash01(Math.floor(distance * ledCount), Math.floor(nowMs / 75));
        return paletteByte(noise * 255, Math.min(1, audioLevel * effectAmount + 0.12));
    }
    if (preset === 11) {
        const balls = 1 + (effectByte >> 5);
        const color = [0, 0, 0];
        for (let history = 0; history < 10; history++) {
            const historyTime = Math.max(0, t - history / 35);
            const decay = Math.pow(0.92, history);
            for (let i = 0; i < balls; i++) {
                const bpmValue = speedByte / 4 + i * 2;
                const position = 0.5 + Math.sin(historyTime * bpmValue / 60 * Math.PI * 2) * 0.5;
                const footprint = Math.max(0, 1 - Math.abs(stripPos - position) * ledCount / 1.15);
                if (footprint <= 0) continue;
                const add = paletteByte(historyTime * 1000 / 4 + i * 2, footprint * decay * audioLevel);
                color[0] = clampByte(color[0] + add[0]);
                color[1] = clampByte(color[1] + add[1]);
                color[2] = clampByte(color[2] + add[2]);
            }
        }
        return color;
    }
    if (preset === 12) {
        let color = [0, 0, 0];
        const stepSeconds = Math.max(0.001, (255 - speedByte) / 4 / 1000);
        const travelTime = ledCount * stepSeconds;
        for (let comet = 0; comet < 7; comet++) {
            const cycleTime = travelTime + 0.55 + comet * 0.11;
            const age = fract((t + hash01(comet, 12) * cycleTime) / cycleTime) * cycleTime;
            if (age > travelTime) continue;
            const head = age / travelTime;
            const behind = head - stripPos;
            if (behind < 0) continue;
            const trail = Math.exp(-behind * ledCount * 0.75) * (0.35 + audioLevel * 0.9);
            if (trail > 0.01) color = addRgb(color, paletteByte(stripPos * 255, trail));
        }
        return color;
    }
    if (preset === 13) {
        const width = Math.min(1, audioLevel * 2 * effectAmount);
        if (stripPos >= width) return [0, 0, 0];
        const noise = hash01(ledIndex * Math.max(1, Math.round(audioLevel * 255)), Math.floor(nowMs / 80));
        return paletteByte(noise * 255);
    }
    if (preset === 14) {
        const phaseA = signedBeat(6, t, -4, 4) * t * 35;
        const phaseB = signedBeat(7, t, -4, 4) * t * 35;
        const frequencyA = 1 + Math.floor(3 * speedByte / 32);
        const frequencyB = 97 + Math.floor(5 * speedByte / 32);
        const field = (cubicWave8(ledIndex * frequencyA + phaseA) >> 1) + (byteCos(ledIndex * frequencyB + phaseB) >> 1);
        const threshold = audioLevel * 255 * effectByte / 64;
        return field <= threshold ? paletteByte(field, field / 255) : [0, 0, 0];
    }
    if (preset === 16) {
        const bandsShown = 1 + Math.floor(clampByte(controlValue(effect, "custom 1", 255)) * 15 / 255);
        let columns = Math.max(2, bandsShown);
        columns = Math.min(columns, Math.floor((ledCount + 1) / 2));
        const rows = Math.floor(ledCount / Math.max(1, columns));
        if (columns <= 1 || rows <= 1) return [0, 0, 0];
        const offset = Math.floor((ledCount - rows * columns + 1) / 2);
        const relative = ledIndex - offset;
        if (relative < 0 || relative >= rows * columns) return [0, 0, 0];
        const column = Math.floor(relative / rows);
        const arranged = relative % rows;
        let heightIndex = -1;
        for (let height = 0; height < rows; height++) {
            const mapped = rows > 4
                ? ((height & 1) ? Math.floor((rows + height) / 2) : Math.floor((rows - 1 - height) / 2))
                : height;
            if (mapped === arranged) { heightIndex = height; break; }
        }
        const logicalBand = Math.floor(column * bandsShown / columns);
        const bandIndex = bandsShown > 1 ? Math.floor(logicalBand * 15 / (bandsShown - 1)) : 0;
        const barHeight = Math.floor(audioBand(audio, bandIndex) * rows);
        if (heightIndex >= 0 && heightIndex < barHeight) {
            const colorIndex = controlValue(effect, "option 1", false)
                ? heightIndex * 255 / Math.max(1, rows - 1)
                : bandIndex * 17;
            return paletteByte(colorIndex);
        }
        if (heightIndex === Math.min(rows - 1, barHeight + 1)) return scaled(secondaryRgb, 0.7);
        return [0, 0, 0];
    }
    if (preset === 17) {
        const reach = Math.min(0.5, audioLevel * effectByte / 256);
        const distance = Math.abs(stripPos - 0.5);
        if (Math.abs(distance - reach) <= 1.2 / ledCount) return scaled([128, 128, 128], 0.85);
        if (distance >= reach || reach <= 0) return [0, 0, 0];
        return paletteByte(peakPosition * 255 + nowMs / 64, 0.82);
    }
    if (preset === 18) {
        const center = Math.floor(ledCount / 2);
        const distance = Math.abs(ledIndex - center);
        const stepSeconds = Math.max(0.001, (256 - speedByte) * 0.0005);
        const historyTime = Math.max(0, t - distance * stepSeconds);
        const peakHz = 80 * Math.pow(11025 / 80, peakPosition);
        const low = 80 + 3 * clampByte(controlValue(effect, "custom 1", 9));
        const high = 80 + 42 * clampByte(controlValue(effect, "custom 2", 255));
        let hue = high === low ? 0 : (peakHz - low) * 255 / (high - low);
        hue += Math.sin(historyTime * 2.4) * 12;
        const preamp = Math.max(1, Math.min(31, clampByte(controlValue(effect, "custom 3", 9))));
        const sensitivity = 0.5 * (0.5 + (preamp - 1) * 9.5 / 30);
        const brightness = Math.min(255, audioLevel * 255 * effectAmount * sensitivity);
        return scaled(hsvToRgbPreview(hue, 176 + brightness / 4, brightness), 0.78 + 0.22 * Math.sin(historyTime * 5) ** 2);
    }
    if (preset === 19) {
        const lampBpm = Math.max(1, clampByte(controlValue(effect, "bpm", 7)));
        const radius = Math.max(0.08, clampByte(controlValue(effect, "radius", 24)) / 50);
        const intensity = clampByte(controlValue(effect, "intensity", 190)) / 255;
        const phase = t * lampBpm / 60 * Math.PI * 2;
        let field = 0;
        const phaseX = [0, 1.95, 3.9];
        const phaseY = [Math.PI / 2, 4.9, 2.45];
        const speeds = [1, 2, 1];
        for (let blob = 0; blob < 3; blob++) {
            const bx = 0.5 + Math.sin(phase * speeds[blob] + phaseX[blob]) * 0.42;
            const by = 0.5 + Math.sin(phase * speeds[blob] + phaseY[blob]) * 0.42;
            const dx = x - bx;
            const dy = y - by;
            field += radius * radius * 0.22 / (dx * dx + dy * dy + 0.008);
        }
        const value = Math.min(1, field * intensity);
        return palc(value, Math.min(1, 0.16 + value * 1.15));
    }
    if (preset === 20) {
        const freqX = Math.max(1, Number(controlValue(effect, "freq_x", 2)) || 2);
        const freqY = Math.max(1, Number(controlValue(effect, "freq_y", 3)) || 3);
        const waveSpeed = clampByte(controlValue(effect, "speed", 28)) / 100;
        const phase = t * waveSpeed;
        const sx = 0.5 + 0.5 * Math.sin((x * freqX + phase) * Math.PI * 2);
        const sy = 0.5 + 0.5 * Math.sin((y * freqY + phase * 1.3) * Math.PI * 2);
        return palc((sx + sy) * 0.5);
    }
    if (preset === 21) {
        const plasmaBpm = Math.max(1, clampByte(controlValue(effect, "bpm", 14)));
        const scaleX = Math.max(1, Number(controlValue(effect, "scale_x", 96)) || 96);
        const scaleY = Math.max(1, Number(controlValue(effect, "scale_y", 112)) || 112);
        const hueShift = clampByte(controlValue(effect, "hue_shift", 8)) / 255;
        const phase = t * plasmaBpm / 60;
        const freqX = 256 / scaleX;
        const freqY = 256 / scaleY;
        const s1 = 0.5 + 0.5 * Math.sin((x * freqX + phase) * Math.PI * 2);
        const s2 = 0.5 + 0.5 * Math.sin((y * freqY + phase * 2) * Math.PI * 2);
        const s3 = 0.5 + 0.5 * Math.sin(((x + y) * freqX - phase * 3) * Math.PI * 2);
        const s4 = 0.5 + 0.5 * Math.cos((x * freqY - y * freqX + phase) * Math.PI * 2);
        return palc((s1 + s2 + s3 + s4) * 0.25 + hueShift);
    }
    if (preset === 22) {
        const balls = Math.max(1, Math.min(8, clampByte(controlValue(effect, "count", 2))));
        const ballBpm = Math.max(1, clampByte(controlValue(effect, "bpm", 10)));
        const radius = Math.max(0.04, clampByte(controlValue(effect, "radius", 20)) / 50);
        const hueShift = clampByte(controlValue(effect, "hue_shift", 24)) / 255;
        const phase = t * ballBpm / 60 * Math.PI * 2;
        let field = 0;
        for (let ball = 0; ball < balls; ball++) {
            const offset = ball * Math.PI;
            const bx = 0.5 + Math.sin(phase + offset) * 0.43;
            const by = 0.5 + Math.cos(phase * 1.35 + offset) * 0.43;
            const dx = x - bx;
            const dy = y - by;
            field += radius * radius * 0.18 / (dx * dx + dy * dy + 0.006);
        }
        const brightness = Math.min(1, field);
        return palc(field * 0.5 + hueShift, Math.min(1, 0.12 + brightness * 1.25));
    }
    return palc(u + pt * 0.06, 0.85);
}

export function nebuliteRainbowPreviewColor(effect, t, x, count) {
    const palette = [[255,0,0],[213,42,0],[171,85,0],[171,127,0],
        [171,171,0],[86,213,0],[0,255,0],[0,213,42],
        [0,171,85],[0,86,170],[0,0,255],[42,0,213],
        [85,0,171],[127,0,129],[171,0,85],[213,0,43]];
    const lights = Math.max(1, Math.round(Number(count) || HELIO_DEFAULT_LED_COUNT));
    const first = Math.min(lights, Math.max(0, Math.trunc(Number(controlValue(effect, "segment start", 0)) || 0)));
    const rawStop = Math.max(0, Math.trunc(Number(controlValue(effect, "segment stop", 0)) || 0));
    const stop = rawStop ? Math.min(lights, rawStop) : lights;
    const pixel = Math.min(lights - 1, Math.floor(Math.max(0, Math.min(0.999999, Number(x) || 0)) * lights));
    if (pixel < first || pixel >= stop) return [0, 0, 0];
    const speed = clampByte(controlValue(effect, "speed", 128));
    const size = clampByte(controlValue(effect, "size", 128));
    const phase = (Math.floor(Math.max(0, Number(t) || 0) * 1000) * ((speed >> 2) + 2)) & 65535;
    const index = (Math.floor((pixel - first) * (16 << Math.floor(size / 29)) / (stop - first)) + (phase >> 8)) & 255;
    const a = palette[index >> 4], b = palette[((index >> 4) + 1) & 15], lo = index & 15;
    if (!lo) return [...a];
    const f2 = lo << 4, f1 = 255 - f2;
    return a.map((v, channel) => ((v * (f1 + 1)) >> 8) + ((b[channel] * (f2 + 1)) >> 8));
}

// Stateless illustration: nominal frame spacing is used to represent WLED's
// forced first frame after a cycle; delayed real frames can differ.
export function nebuliteBlinkPreviewColor(effect, t, x, count, opts = {}) {
    const lights = Math.max(1, Math.round(Number(count) || HELIO_DEFAULT_LED_COUNT));
    const first = Math.min(lights, Math.max(0, Math.trunc(Number(controlValue(effect, "segment start", 0)) || 0)));
    const rawStop = Math.max(0, Math.trunc(Number(controlValue(effect, "segment stop", 0)) || 0));
    const stop = rawStop ? Math.min(lights, rawStop) : lights;
    const pixel = Math.min(lights - 1, Math.floor(Math.max(0, Math.min(0.999999, Number(x) || 0)) * lights));
    if (pixel < first || pixel >= stop) return [0, 0, 0];
    const speed = clampByte(controlValue(effect, "speed", 230));
    const intensity = clampByte(controlValue(effect, "effect intensity", 128));
    const frame = Math.max(1, Math.min(1000, Math.round(Number(controlValue(effect, "frame ms", 23)) || 23)));
    const blend = Math.max(0, Math.min(3, Math.trunc(Number(controlValue(effect, "palette blend", 0)) || 0)));
    const now = Math.floor(Math.max(0, Number(t) || 0) * 1000) >>> 0;
    const duration = (255 - speed) * 20, cycle = duration + frame * 2;
    const firstFrame = now >= frame && Math.floor(now / cycle) !== Math.floor((now - frame) / cycle);
    const on = firstFrame || now % cycle <= frame + ((duration * intensity) >> 8);
    const primary = ["red", "green", "blue"].map((name, i) => clampByte(controlValue(effect, name, i === 2 ? 255 : 0)));
    const secondary = ["secondary red", "secondary green", "secondary blue"].map(name => clampByte(controlValue(effect, name, 0)));
    const white = clampByte(controlValue(effect, "secondary white", 0));
    const palettePhase = on || (white === 0 && primary.every((v, i) => v === secondary[i]));
    if (!palettePhase) {
        return opts.rgbOnly ? secondary : rgbwToRgb([...secondary, white]);
    }
    let index = stop - first > 1 ? Math.floor((pixel - first) * 255 / (stop - first - 1)) : 0;
    if (blend !== 1 && blend !== 3) index = (index * 241) >> 8;
    const slot = index >> 4, frac = index & 15;
    const entry = n => n <= 5 ? primary : n >= 10 ? secondary : primary.map((v, c) =>
        (v * 256 + Math.trunc((secondary[c] - v) * 128 / 5) * 2 * (n - 5)) >> 8);
    const a = entry(slot);
    if (!frac || blend === 3) return [...a];
    const b = entry((slot + 1) & 15), f2 = frac << 4, f1 = 255 - f2;
    return a.map((v, c) => ((v * (f1 + 1)) >> 8) + ((b[c] * (f2 + 1)) >> 8));
}

export function nebuliteSolidPreviewColor(effect, x, count, opts = {}) {
    const lights = Math.max(1, Math.round(Number(count) || HELIO_DEFAULT_LED_COUNT));
    const first = Math.min(lights, Math.max(0, Math.trunc(Number(controlValue(effect, "segment start", 0)) || 0)));
    const rawStop = Math.max(0, Math.trunc(Number(controlValue(effect, "segment stop", 0)) || 0));
    const stop = rawStop ? Math.min(lights, rawStop) : lights;
    const pixel = Math.min(lights - 1, Math.floor(Math.max(0, Math.min(0.999999, Number(x) || 0)) * lights));
    if (pixel < first || pixel >= stop) return [0, 0, 0];
    const brightness = clampByte(controlValue(effect, "brightness", 255));
    const channels = ["red", "green", "blue", "white"].map((name, i) =>
        Math.floor((clampByte(controlValue(effect, name, i === 0 ? 255 : 0)) * brightness + 127) / 255));
    return opts.rgbOnly ? channels.slice(0, 3) : rgbwToRgb(channels);
}

const paletteStates = new WeakMap();
export function nebulitePalettePreviewColor(effect, t, x, count) {
    const lights=Math.max(1,Math.round(Number(count)||HELIO_DEFAULT_LED_COUNT));
    const first=Math.min(lights,Math.max(0,Math.trunc(Number(controlValue(effect,'segment start',0))||0)));
    const end=Math.max(0,Math.trunc(Number(controlValue(effect,'segment stop',0))||0));
    const stop=end?Math.min(lights,end):lights;
    const pixel=Math.min(lights-1,Math.floor(Math.max(0,Math.min(.999999,Number(x)||0))*lights));
    if(pixel<first||pixel>=stop)return [0,0,0];
    const now=Math.floor(Math.max(0,Number(t)||0)*1000)>>>0;
    const frame=Math.max(1,Math.min(1000,Math.round(Number(controlValue(effect,'frame ms',23))||23)));
    let state=paletteStates.get(effect);
    if(!state||now<state.now)state={frame:now,now};
    else if(((now-state.frame)>>>0)>=frame)state.frame=now;
    state.now=now;paletteStates.set(effect,state);
    const speed=clampByte(controlValue(effect,'speed',128));
    const counter=speed?((Math.imul(state.frame,(speed>>3)+1)&65535)>>>8):0;
    let index=(Math.floor((pixel-first)*255/(stop-first))-counter)&255;
    if(!speed)index=(index*241)>>8;
    const primary=['red','green','blue'].map((n,i)=>clampByte(controlValue(effect,n,[0,255,200][i])));
    const secondary=['secondary red','secondary green','secondary blue'].map((n,i)=>clampByte(controlValue(effect,n,[255,0,255][i])));
    const entry=n=>n<=5?primary:n>=10?secondary:primary.map((v,c)=>(v*256+Math.trunc((secondary[c]-v)*128/5)*2*(n-5))>>8);
    const a=entry(index>>4),lo=index&15;if(!lo)return [...a];
    const b=entry(((index>>4)+1)&15),f=lo<<4;
    return a.map((v,c)=>((v*(256-f))>>8)+((b[c]*(f+1))>>8));
}

// Source simulation for the experimental wearable effect. A preview owns its
// RNG and frame history; it is not a mirror of the controller's random seed.
const washingStates = new WeakMap();
const washingScale = (v, n) => (v * (n + 1)) >> 8;
const washingVideo = (v, n) => ((v * n) >> 8) + (v && n ? 1 : 0);
function washingRgb(h, s, v) {
    const o = (h & 31) << 3, a = washingScale(o, 85), b = washingScale(o, 170);
    let rgb = [[255-a,a,0], [171,85+a,0], [171-b,170+a,0], [0,255-a,a],
        [0,171-b,85+b], [a,0,255-a], [85+a,0,171-a], [170+a,0,85-a]][h >> 5];
    if (s !== 255) {
        if (!s) rgb = [255,255,255];
        else { const d = washingVideo(255-s, 255-s); rgb = rgb.map(c => washingScale(c,255-d)+d); }
    }
    if (v !== 255) { const b = washingVideo(v,v); rgb = rgb.map(c => washingScale(c,b)); }
    return rgb;
}
function washingPalette(state) {
    const draw = () => { let x = state.rng; x ^= x << 13; x ^= x >>> 17; x ^= x << 5; state.rng = x >>> 0; return state.rng; };
    const anchors = Array.from({length:4}, () => [draw() >>> 24, 160 + (((draw() >>> 16) * 95) >>> 16), 128 + (((draw() >>> 16) * 127) >>> 16)]);
    const result = Array(16);
    for (let k=0;k<3;k++) {
        const a = anchors[k], b = anchors[k+1], hue = (b[0]-a[0]) & 255;
        const dist = hue > 127 ? -((256-hue)&255) : hue;
        const delta = [dist, b[1]-a[1], b[2]-a[2]].map(v => Math.trunc(v*128/5)*2);
        const acc = a.map(v => v << 8);
        for (let n=0;n<=5;n++) {
            result[k*5+n] = washingRgb(...acc.map(v => (v >>> 8)&255));
            for (let c=0;c<3;c++) acc[c]=(acc[c]+delta[c])&65535;
        }
    }
    return result;
}
function washingSine(value) {
    let a = (value & 255) * 257, sign=1;
    if (a > 32767) { a=65535-a; sign=-1; }
    const p=a*(32767-a), result=Math.floor(p*131068/(1342095361-p))*sign;
    return Math.min(result+32767+128,65535) >> 8;
}
// FastLED 3.6 fixed 2D noise, matching WledFastNoise; MIT attribution in docs/licenses.
const noisefirePerm = [
    151, 160, 137,  91,  90,  15, 131,  13, 201,  95,  96,  53, 194, 233,   7, 225,
    140,  36, 103,  30,  69, 142,   8,  99,  37, 240,  21,  10,  23, 190,   6, 148,
    247, 120, 234,  75,   0,  26, 197,  62,  94, 252, 219, 203, 117,  35,  11,  32,
     57, 177,  33,  88, 237, 149,  56,  87, 174,  20, 125, 136, 171, 168,  68, 175,
     74, 165,  71, 134, 139,  48,  27, 166,  77, 146, 158, 231,  83, 111, 229, 122,
     60, 211, 133, 230, 220, 105,  92,  41,  55,  46, 245,  40, 244, 102, 143,  54,
     65,  25,  63, 161,   1, 216,  80,  73, 209,  76, 132, 187, 208,  89,  18, 169,
    200, 196, 135, 130, 116, 188, 159,  86, 164, 100, 109, 198, 173, 186,   3,  64,
     52, 217, 226, 250, 124, 123,   5, 202,  38, 147, 118, 126, 255,  82,  85, 212,
    207, 206,  59, 227,  47,  16,  58,  17, 182, 189,  28,  42, 223, 183, 170, 213,
    119, 248, 152,   2,  44, 154, 163,  70, 221, 153, 101, 155, 167,  43, 172,   9,
    129,  22,  39, 253,  19,  98, 108, 110,  79, 113, 224, 232, 178, 185, 112, 104,
    218, 246,  97, 228, 251,  34, 242, 193, 238, 210, 144,  12, 191, 179, 162, 241,
     81,  51, 145, 235, 249,  14, 239, 107,  49, 192, 214,  31, 181, 199, 106, 157,
    184,  84, 204, 176, 115, 121,  50,  45, 127,   4, 150, 254, 138, 236, 205,  93,
    222, 114,  67,  29,  24,  72, 243, 141, 128, 195,  78,  66, 215,  61, 156, 180,
    151];
const noisefireScale = (v, f) => (v * (f + 1)) >> 8;
const noisefireI8 = v => (v << 24) >> 24;
function noisefireNoise(x, y) {
    const X=x>>>8, Y=y>>>8, p=noisefirePerm;
    const A=(p[X]+Y)&255, B=(p[X+1]+Y)&255;
    const ease=v=>{let j=(v&128)?255-v:v, r=(noisefireScale(j,j)<<1)&255;return (v&128)?255-r:r;};
    const grad=(h,x,y)=>{let u=(h&4)?y:x,v=(h&4)?x:y;if(h&1)u=noisefireI8(-u);if(h&2)v=noisefireI8(-v);return noisefireI8((u>>1)+(v>>1)+(u&1));};
    const mix=(a,b,f)=>noisefireI8(b>a?a+noisefireScale(b-a,f):a-noisefireScale(a-b,f));
    const xx=(x&255)>>1, yy=(y&255)>>1, u=ease(x&255),v=ease(y&255);
    const a=mix(grad(p[p[A]],xx,yy),grad(p[p[B]],noisefireI8(xx-128),yy),u);
    const b=mix(grad(p[p[A+1]],xx,noisefireI8(yy-128)),grad(p[p[B+1]],noisefireI8(xx-128),noisefireI8(yy-128)),u);
    return Math.min(255,((mix(a,b,v)+64)&255)*2);
}
const noisefirePalette = [2,4,8,8,16].map(v=>washingRgb(0,255,v)).concat(
    [[255,0,0],[255,0,0],[255,0,0],[255,140,0],[255,140,0],
     [255,165,0],[255,165,0],[255,255,0],[255,165,0],[255,255,0],[255,255,0]]);
const noisefireStates = new WeakMap();
function legacySimulatedVolume(now) {
    return washingSine((Math.imul(Math.imul(now, 13 << 8), 280) >>> 24) & 255);
}
export function nebuliteNoisefirePreviewColor(effect,t,x,count,audio=HELIO_PREVIEW_AUDIO,opts={}) {
    const lights=Math.min(65535,Math.max(1,Math.round(Number(count)||HELIO_DEFAULT_LED_COUNT)));
    const first=Math.min(lights,Math.max(0,Math.trunc(Number(controlValue(effect,'segment start',0))||0)));
    const end=Math.max(0,Math.trunc(Number(controlValue(effect,'segment stop',0))||0)),stop=end?Math.min(lights,end):lights;
    const pixel=Math.min(lights-1,Math.floor(Math.max(0,Math.min(.999999,Number(x)||0))*lights));
    if(pixel<first||pixel>=stop)return [0,0,0];
    const now=Math.floor(Math.max(0,Number(t)||0)*1000+1e-7)>>>0;
    const interval=Math.max(1,Math.min(1000,Math.round(Number(controlValue(effect,'frame ms',23))||23)));
    let state=noisefireStates.get(effect);
    if(!state||now<state.now) {state={frame:now,now,started:false,brightness:0};noisefireStates.set(effect,state);}
    if(!state.started||((now-state.frame)>>>0)>=interval){
        state.frame=now;state.started=true;
        const simulated=opts.audioSourcePresent===false && controlValue(effect,'simulate if no source',false)===true;
        const input=simulated?legacySimulatedVolume(now):Math.round(Math.max(0,Math.min(1,Number(audio?.level)||0))*255);
        const level=Math.max(clampByte(controlValue(effect,'audio floor',0)),input);
        state.brightness=level>=128?255:level*2;
    }
    state.now=now;
    const speed=clampByte(controlValue(effect,'speed',128)),intensity=clampByte(controlValue(effect,'effect intensity',128));
    const n=stop-first,pos=pixel-first,xn=Math.floor(pos*speed/64)&65535;
    const step=Math.floor((Math.imul(state.frame,speed)>>>0)/64), yn=Math.floor((Math.imul(step,n)>>>0)/255)&65535;
    const noise=noisefireNoise(xn,yn),index=Math.floor((255-Math.floor(pos*256/n))*noise/(256-intensity))&255;
    const slot=index>>4,lo=index&15,a=noisefirePalette[slot],b=noisefirePalette[(slot+1)&15],f=lo<<4;
    const rgb=lo?a.map((c,i)=>noisefireScale(c,255-f)+noisefireScale(b[i],f)):a.slice();
    return rgb.map(c=>state.brightness===255?c:state.brightness===0?0:noisefireScale(c,state.brightness+1));
}

const flowStripeStates = new WeakMap();
// A simulated audio level and independent RNG keep this a preview, not a
// representation of the microphone or random palette on the controller.
const plasmoidStates = new WeakMap();
export function nebulitePlasmoidPreviewColor(effect, t, x, count, audio = HELIO_PREVIEW_AUDIO, opts = {}) {
    const lights = Math.min(65535, Math.max(1, Math.round(Number(count) || HELIO_DEFAULT_LED_COUNT)));
    const first = Math.min(lights, Math.max(0, Math.trunc(Number(controlValue(effect, 'segment start', 0)) || 0)));
    const end = Math.max(0, Math.trunc(Number(controlValue(effect, 'segment stop', 0)) || 0));
    const stop = end ? Math.min(lights, end) : lights;
    const now = Math.floor(Math.max(0, Number(t) || 0) * 1000 + 1e-7) >>> 0;
    const interval = Math.max(1, Math.min(1000, Math.round(Number(controlValue(effect, 'frame ms', 23)) || 23)));
    let state = plasmoidStates.get(effect);
    if (!state || now < state.now || state.lights !== lights || state.first !== first || state.stop !== stop) {
        state = {rng: 0x1234abcd, lights, first, stop, now, frame: now, paletteStart: now,
            started: false, a: 0, b: 0, pixels: Array.from({length: lights}, () => [0,0,0,0])};
        state.current = washingPalette(state); state.previous = state.current;
        plasmoidStates.set(effect, state);
    }
    if (state.started && ((now - state.paletteStart) >>> 0) > 5000) {
        state.previous = state.current; state.current = washingPalette(state); state.paletteStart = now;
    }
    if (stop > first && (!state.started || ((now - state.frame) >>> 0) >= interval)) {
        const elapsed = (now - state.paletteStart) >>> 0, steps = Math.floor(128 * elapsed / 250);
        const entry = n => elapsed > 250 ? state.current[n] : state.previous[n].map((v,c) => {
            const target = state.current[n][c];
            return v < target ? Math.min(target, v + steps) : Math.max(target, v - steps * 2);
        });
        const beat = bpm => (252 + washingScale(washingSine((Math.imul(Math.imul(now, bpm << 8), 280) >>> 24)), 8)) & 255;
        state.a = (state.a + beat(6)) & 255; state.b = (state.b + beat(7)) & 255;
        const speed = clampByte(controlValue(effect, 'speed', 41));
        const intensity = clampByte(controlValue(effect, 'effect intensity', 112));
        const floor = clampByte(controlValue(effect, 'audio floor', 0));
        const simulated = opts.audioSourcePresent === false && controlValue(effect, 'simulate if no source', false) === true;
        const input = simulated ? legacySimulatedVolume(now) : Math.round(Math.max(0, Math.min(1, Number(audio?.level) || 0)) * 255);
        const level = Math.max(floor, input);
        const base = ['secondary red','secondary green','secondary blue','secondary white'].map(n => clampByte(controlValue(effect,n,0)));
        for (let i = first; i < stop; i++) {
            const pos = i - first, phase = (pos * (1 + Math.floor(3 * speed / 32)) + state.a) & 255;
            const triangle = (phase & 128 ? 255 - phase : phase) * 2;
            const square = washingScale(triangle, triangle), cube = washingScale(square, triangle);
            const cubic = Math.min(255, 3 * square - 2 * cube);
            const other = (pos * (97 + Math.floor(5 * speed / 32)) + state.b + 64) & 255;
            const index = Math.floor(cubic / 2) + Math.floor(washingSine(other) / 2);
            const amount = level * intensity / 64 < index ? 0 : index;
            const paletteIndex = washingScale(index, 240), slot = paletteIndex >> 4, lo = paletteIndex & 15;
            const a = entry(slot), b = entry((slot + 1) & 15), f = lo << 4;
            const color = lo ? a.map((v,c) => washingScale(v, 255 - f) + washingScale(b[c], f)) : a;
            const target = [...color, 0];
            const add = base.map((v,c) => v === target[c] || !amount ? v : amount === 255 ? target[c] :
                (((v << 8) + target[c] + target[c] * amount - v * amount) >> 8));
            const faded = state.pixels[i].map((v,c) => c === 3 ? 0 : (v * 208) >> 8);
            const sum = faded.map((v,c) => v + add[c]), max = Math.max(...sum);
            state.pixels[i] = max > 255 ? sum.map(v => Math.floor(v * 255 / max)) : sum;
        }
        state.frame = now; state.started = true;
    }
    state.now = now;
    const pixel = Math.min(lights - 1, Math.floor(Math.max(0, Math.min(.999999, Number(x) || 0)) * lights));
    return opts.rgbOnly ? state.pixels[pixel].slice(0,3) : rgbwToRgb(state.pixels[pixel]);
}
export function nebuliteFlowStripePreviewColor(effect, t, x, count) {
    const lights = Math.min(65535, Math.max(1, Math.round(Number(count) || HELIO_DEFAULT_LED_COUNT)));
    const first = Math.min(lights, Math.max(0, Math.trunc(Number(controlValue(effect, 'segment start', 0)) || 0)));
    const end = Math.max(0, Math.trunc(Number(controlValue(effect, 'segment stop', 0)) || 0));
    const stop = end ? Math.min(lights, end) : lights;
    const pixel = Math.min(lights - 1, Math.floor(Math.max(0, Math.min(.999999, Number(x) || 0)) * lights));
    const half = Math.floor((stop - first) * 10 / 13);
    if (pixel < first || pixel >= stop || half < 1) return [0, 0, 0];
    const now = Math.floor(Math.max(0, Number(t) || 0) * 1000 + 1e-7) >>> 0;
    const interval = Math.max(1, Math.min(1000, Math.round(Number(controlValue(effect, 'frame ms', 23)) || 23)));
    let state = flowStripeStates.get(effect);
    if (!state || now < state.now) state = { frame: now, now };
    else if (((now - state.frame) >>> 0) >= interval) state.frame = now;
    state.now = now; flowStripeStates.set(effect, state);
    const speed = clampByte(controlValue(effect, 'speed', 255));
    const intensity = clampByte(controlValue(effect, 'effect intensity', 255));
    const time = Math.floor(state.frame / (Math.floor(intensity / 8) + 1));
    const c = washingSine(Math.floor(Math.abs(pixel - first - half) / half) * 127);
    const wave = washingSine(Math.floor(c / 2) + time);
    const hue = (washingSine(wave + Math.floor(time / 8)) + Math.floor(state.frame / (speed + 1))) & 255;
    return washingRgb(hue, 255, 255);
}
export function nebuliteWashingPreviewColor(effect, t, x, count) {
    const lights = Math.max(1, Math.round(Number(count) || HELIO_DEFAULT_LED_COUNT));
    const first = Math.min(lights, Math.max(0, Math.trunc(Number(controlValue(effect, 'segment start', 0)) || 0)));
    const rawStop = Math.max(0, Math.trunc(Number(controlValue(effect, 'segment stop', 0)) || 0));
    const stop = rawStop ? Math.min(lights, rawStop) : lights;
    const pixel = Math.min(lights-1,Math.floor(Math.max(0,Math.min(0.999999,Number(x)||0))*lights));
    if (pixel < first || pixel >= stop) return [0,0,0];
    const now = Math.floor(Math.max(0,Number(t)||0)*1000) >>> 0;
    const speed = clampByte(controlValue(effect,'speed',128));
    const intensity = clampByte(controlValue(effect,'effect intensity',128));
    const frame = Math.max(1,Math.min(1000,Math.round(Number(controlValue(effect,'frame ms',23))||23)));
    let state = washingStates.get(effect);
    if (!state || now < state.now) {
        state = {rng:0x1234abcd, phase:0, now, frame:now, paletteStart:now};
        state.current = washingPalette(state); state.previous = state.current;
        washingStates.set(effect,state);
        state.first = true;
    }
    if (((now-state.paletteStart)>>>0)>5000) { state.previous=state.current; state.current=washingPalette(state); state.paletteStart=now; }
    if (state.first || ((now-state.frame)>>>0)>=frame) {
        let phase=(now>>>7)&255, amplitude=127;
        if (phase>127) { amplitude=-127; phase-=127; }
        const velocity=phase<15 ? Math.trunc(phase*amplitude/15) : phase<75 ? amplitude : phase<90 ? Math.trunc((90-phase)*amplitude/15) : 0;
        state.phase=(state.phase+Math.trunc(velocity*2048/(512-speed)))>>>0;
        state.frame=now; state.first=false;
    }
    state.now=now;
    const p=state.phase, spatial=Math.floor((Math.floor(intensity/25)+1)*255*(pixel-first)/(stop-first));
    const index=washingScale(washingSine((spatial+(p>>>7))&255),240);
    const elapsed=Math.min((now-state.paletteStart)>>>0,5000), entry=n => state.previous[n].map((v,c) =>
        Math.floor(v*(5000-elapsed)/5000)+Math.floor(state.current[n][c]*elapsed/5000));
    const slot=index>>4, lo=index&15, a=entry(slot);
    if (!lo) return a;
    const b=entry((slot+1)&15), f=lo<<4;
    return a.map((v,c)=>washingScale(v,255-f)+washingScale(b[c],f));
}

// Independent preview state; its random palette is not the controller's live palette.
const sineStates = new WeakMap();
export function nebuliteSinePreviewColor(effect, t, x, count, opts = {}) {
    const lights = Math.max(1, Math.round(Number(count) || HELIO_DEFAULT_LED_COUNT));
    const first = Math.min(lights, Math.max(0, Math.trunc(Number(controlValue(effect, 'segment start', 0)) || 0)));
    const rawStop = Math.max(0, Math.trunc(Number(controlValue(effect, 'segment stop', 0)) || 0));
    const stop = rawStop ? Math.min(lights, rawStop) : lights;
    const pixel = Math.min(lights-1, Math.floor(Math.max(0, Math.min(0.999999, Number(x)||0))*lights));
    if (pixel < first || pixel >= stop) return [0,0,0];
    const now = Math.floor(Math.max(0, Number(t)||0)*1000) >>> 0;
    const speed = clampByte(controlValue(effect, 'speed', 128));
    const intensity = clampByte(controlValue(effect, 'effect intensity', 128));
    const frame = Math.max(1, Math.min(1000, Math.round(Number(controlValue(effect, 'frame ms', 23))||23)));
    let state = sineStates.get(effect);
    if (!state || now < state.now) {
        state = {rng:0x1234abcd, step:0, now, frame:now, paletteStart:now, first:true};
        state.current = washingPalette(state); state.previous = state.current;
        sineStates.set(effect, state);
    }
    if (state.first || ((now-state.frame)>>>0)>=frame) {
        if (((now-state.paletteStart)>>>0)>5000) { state.previous=state.current; state.current=washingPalette(state); state.paletteStart=now; }
        state.step=(state.step+Math.floor(speed/16))>>>0; state.frame=now; state.first=false;
    }
    state.now=now;
    const phase = ((pixel-first)*Math.floor(intensity/4)+state.step)&255;
    const triangle = (phase&128 ? 255-phase : phase)*2;
    const square = washingScale(triangle,triangle), cube = washingScale(square,triangle);
    const amount = Math.min(255, 3*square-2*cube);
    const index = washingScale(Math.floor((pixel-first)*((state.frame>>>5)&65535)/255)&255,240);
    const elapsed=Math.min((state.frame-state.paletteStart)>>>0,5000), entry=n => state.previous[n].map((v,c) =>
        Math.floor(v*(5000-elapsed)/5000)+Math.floor(state.current[n][c]*elapsed/5000));
    const slot=index>>4, lo=index&15, a=entry(slot), b=entry((slot+1)&15), f=lo<<4;
    const color = lo ? a.map((v,c)=>washingScale(v,255-f)+washingScale(b[c],f)) : a;
    const base = ['secondary red','secondary green','secondary blue','secondary white'].map(name => clampByte(controlValue(effect,name,0)));
    const target = [...color,0];
    const rgbw = base.map((v,c) => v === target[c] || amount === 0 ? v : amount === 255 ? target[c] :
        (((v<<8)+target[c]+target[c]*amount-v*amount)>>8));
    return opts.rgbOnly ? rgbw.slice(0,3) : rgbwToRgb(rgbw);
}

// Archived Firefly palette after FastLED 3.6 expansion; pre-driver RGB only.
const fireflyPalette = [[0,0,0],[9,0,0],[18,0,0],[18,0,0],[49,0,0],[81,0,0],
    [112,0,0],[113,0,0],[142,3,1],[175,17,1],[213,44,2],[255,82,4],
    [255,115,4],[255,156,4],[255,203,4],[255,255,71]];
// The archived effect resets its seed every frame. Cache the three fixed bytes
// per segment-relative pixel instead of replaying all preceding pixels.
const fireflyRandomValues = [];
let fireflySeed = 535;
function fireflyRandomAt(pixel) {
    const offset = pixel * 3;
    while (fireflyRandomValues.length <= offset + 2) {
        fireflySeed = (fireflySeed * 2053 + 13849) & 65535;
        fireflyRandomValues.push(((fireflySeed & 255) + (fireflySeed >> 8)) & 255);
    }
    return offset;
}
export function nebuliteFireflyPreviewColor(effect, t, x, count, opts = {}) {
    const requestedCount = Number(count);
    const lights = Math.min(65535, Math.max(1, Math.round(Number.isFinite(requestedCount) && requestedCount > 0 ? requestedCount : HELIO_DEFAULT_LED_COUNT)));
    const first = Math.min(lights, Math.max(0, Math.trunc(Number(controlValue(effect, 'segment start', 0)) || 0)));
    const rawStop = Math.max(0, Math.trunc(Number(controlValue(effect, 'segment stop', 0)) || 0));
    const stop = rawStop ? Math.min(lights, rawStop) : lights;
    const pixel = Math.min(lights - 1, Math.floor(Math.max(0, Math.min(0.999999, Number(x) || 0)) * lights));
    if (pixel < first || pixel >= stop) return [0,0,0];
    const frame = Math.max(1, Math.min(1000, Math.round(Number(controlValue(effect, 'frame ms', 23)) || 23)));
    const now = (Math.floor(Math.max(0, Number(t) || 0) * 1000 / frame) * frame) >>> 0;
    const speed = clampByte(controlValue(effect, 'speed', 55)), intensity = clampByte(controlValue(effect, 'effect intensity', 255));
    const randomOffset = fireflyRandomAt(pixel - first);
    const start = fireflyRandomValues[randomOffset], filter = fireflyRandomValues[randomOffset + 1];
    let index = fireflyRandomValues[randomOffset + 2];
    const phase = (start + Math.floor((Math.imul(16, now) >>> 0) / (256 - speed))) & 255;
    let theta = phase * 257, sign = 1;
    if (theta > 32767) { theta = 65535 - theta; sign = -1; }
    const product = theta * (32767 - theta);
    const sine = Math.min(65535, Math.floor(product * 131068 / (1342095361 - product)) * sign + 32767 + 128) >> 8;
    const amount = filter > intensity ? 0 : sine;
    index = washingScale((index + Math.floor(now / 100)) & 255, 240);
    const slot = index >> 4, lo = index & 15, a = fireflyPalette[slot], b = fireflyPalette[(slot + 1) & 15], f = lo << 4;
    const color = lo ? a.map((v,c) => washingScale(v, 255 - f) + washingScale(b[c], f)) : a;
    const base = ['secondary red','secondary green','secondary blue','secondary white'].map(name => clampByte(controlValue(effect, name, 0)));
    const target = [...color, 0];
    const rgbw = base.map((v,c) => v === target[c] || amount === 0 ? v : amount === 255 ? target[c] :
        (((v << 8) + target[c] + target[c] * amount - v * amount) >> 8));
    return opts.rgbOnly ? rgbw.slice(0,3) : rgbwToRgb(rgbw);
}

const snowStates = new WeakMap();
const snowPalette = [[0,0,0],[0,3,15],[0,6,30],[0,9,45],[0,9,45],[0,18,115],
    [0,28,185],[0,37,255],[0,38,255],[3,100,255],[3,100,255],[23,199,255],
    [23,199,255],[100,235,255],[100,235,255],[255,255,255]];
export function nebuliteSnowflakesPreviewColor(effect, t, x, count, opts = {}) {
    const lights = Math.max(1, Math.min(65535, Math.round(Number(count) || HELIO_DEFAULT_LED_COUNT)));
    const first = Math.min(lights, Math.max(0, Math.trunc(Number(controlValue(effect, 'segment start', 0)) || 0)));
    const end = Math.max(0, Math.trunc(Number(controlValue(effect, 'segment stop', 0)) || 0));
    const stop = end ? Math.min(lights, end) : lights;
    const now = Math.round(Math.max(0, Number(t) || 0) * 1000) >>> 0;
    const frame = Math.max(1, Math.min(1000, Math.round(Number(controlValue(effect, 'frame ms', 24)) || 24)));
    let state = snowStates.get(effect);
    if (!state || now < state.now || state.lights !== lights || state.first !== first || state.stop !== stop) {
        state = {lights, first, stop, now, frame: now, started: false, seed: 1337,
            pixels: Array.from({length: lights}, () => [0,0,0]), rising: new Uint8Array(lights)};
        snowStates.set(effect, state);
    }
    const length = Math.max(0, stop - first);
    if (length && (!state.started || ((now - state.frame) >>> 0) >= frame)) {
        const speed = clampByte(controlValue(effect, 'speed', 35));
        const intensity = clampByte(controlValue(effect, 'effect intensity', 197));
        const referenceBrightness = controlValue(effect, 'source brightness', 255);
        const master = typeof opts.getMasterBrightness === 'function' ? opts.getMasterBrightness() : null;
        const brightness = clampByte(controlValue(effect, 'follow master', true) && Number.isFinite(master) ? master : referenceBrightness);
        const up = brightness > 28 ? 8 + (speed >> 2) : 68 - brightness;
        const down = brightness > 28 ? 8 + (speed >> 3) : 68 - brightness;
        const draw = () => { state.seed = (state.seed * 2053 + 13849) & 65535; return state.seed; };
        const rand8 = () => { const n = draw(); return ((n & 255) + (n >> 8)) & 255; };
        for (let i = first; i < stop; ++i) {
            const old = state.pixels[i];
            if (state.rising[i]) {
                let next = old.map(v => Math.min(255, v + ((v * up) >> 8) + (v && up ? 1 : 0)));
                if (next.some(v => v === 255)) state.rising[i] = 0;
                if (next.every((v, c) => v === old[c])) next = next.map(v => Math.min(255, 2 * v));
                state.pixels[i] = next;
            } else state.pixels[i] = old.map(v => (v * (256 - down)) >> 8);
        }
        for (let j = 0; j <= Math.floor(length / 50); ++j) {
            if (rand8() > intensity) continue;
            for (let attempt = 0; attempt < 5; ++attempt) {
                const i = first + ((draw() * length) >>> 16);
                if (state.pixels[i].some(Boolean)) continue;
                state.pixels[i] = snowPalette[rand8() >> 4].map(v => (v * 66) >> 8);
                state.rising[i] = 1; break;
            }
        }
        state.frame = now; state.started = true;
    }
    state.now = now;
    const pixel = Math.min(lights - 1, Math.floor(Math.max(0, Math.min(.999999, Number(x) || 0)) * lights));
    return [...state.pixels[pixel]];
}

// Sphere effects (src/light/effects/Sphere*.h, SphereRing.h): the strip read as a closed ring, painted by angle.
// Integer ports of the firmware's arithmetic; C++ int division truncates, so every quotient goes through t32.
// A preview owns its state per Effect object; music comes from the preview's audio (bands and level, 0..1),
// stepped through the firmware's 23 ms analyser blocks for the smoothed level and bands.
const t32 = v => Math.trunc(v);
const SPHERE_SIN = [0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739, 9512, 10278, 11039, 11793,
    12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530, 18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
    23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
    30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757, 32767];
function sphereSin16(angle) {
    const th = angle & 65535, q = th >> 14, pos = th & 0x3fff, walk = (q & 1) ? 0x4000 - pos : pos;
    const idx = walk >> 8, frac = walk & 255, a = SPHERE_SIN[idx], b = SPHERE_SIN[idx < 64 ? idx + 1 : 64];
    const v = a + (((b - a) * frac) >> 8);
    return q >= 2 ? -v : v;
}
const sphereSine = a => sphereSin16(a) + 32768;
const sphereCosine = a => sphereSin16((a & 65535) + 16384) + 32768;
const sphereLevel = v => v < 0 ? 0 : (v > 255 ? 255 : v | 0);
const sphereScale8 = (v, s) => ((v * s) + 1 + ((v * s) >> 8)) >> 8;
const sphereAngleOf = (i, count) => Math.floor(i * 65536 / count) & 65535;
const spherePhaseOf = (t, periodMs) => Math.floor((t % periodMs) * 65536 / periodMs) & 65535;
function sphereFromAxis(a, axis) { const f = ((a - axis) & 65535) % 32768; return f > 16384 ? 32768 - f : f; }
function sphereColor(pal, index, brightness) {
    const colors = Array.isArray(pal) && pal.length === 16 ? pal : null;
    const i = index & 255, bri = sphereLevel(brightness);
    let c;
    if (colors) {
        const a = colors[i >> 4], b = colors[((i >> 4) + 1) & 15], frac = (i & 15) * 17;
        c = [0, 1, 2].map(k => (sphereScale8(clampByte(a?.[k]), 255 - frac) + sphereScale8(clampByte(b?.[k]), frac)) & 255);
    } else c = paletteAt(pal, i / 255);
    return bri === 255 ? c : c.map(v => sphereScale8(v, bri));
}
function sphereBrightIndex(pal, start) {
    for (let step = 0; step < 8; ++step) {
        const index = (start + step * 32) & 255, c = sphereColor(pal, index, 255);
        if (c[0] > 110 || c[1] > 110 || c[2] > 110) return index;
    }
    return start & 255;
}
class SphereEnvelope {
    constructor() { this.value = 0; }
    step(target, dt, rise, fall) {
        const goal = Math.max(0, Math.min(255, target)) << 8;
        const rate = Math.min(t32((goal > this.value ? rise : fall) * dt / 10), 256);
        this.value += t32((goal - this.value) * rate / 256);
    }
    level() { return this.value >> 8; }
}
function sphereRandom(mem) { mem.seed = (Math.imul(mem.seed, 1664525) + 1013904223) >>> 0; return mem.seed >>> 16; }
// The analyser's frame from the preview's music. The firmware smooths per 23 ms block (a quarter of the way to the
// level); a preview frame closes the same share of the gap for the time it spans, at most half of it, so a kick
// still stands above the average when the preview draws only a few frames a second.
function sphereAudio(mem, now, audio) {
    const level = Math.round(Math.max(0, Math.min(1, Number(audio?.level) || 0)) * 255);
    const bands = Array.from({ length: 16 }, (_v, i) => Math.round(audioBand(audio, i) * 255));
    const a = mem.audio ??= { at: now, levelSmoothed: level, bandsSmoothed: [...bands] };
    const span = now - a.at;
    if (span > 0) {
        const share = Math.min(0.5, 1 - 0.75 ** (span / 23)), fall = Math.min(0.5, 1 - 0.875 ** (span / 23));
        a.levelSmoothed = t32(a.levelSmoothed + (level - a.levelSmoothed) * share);
        a.bandsSmoothed = a.bandsSmoothed.map((s, i) => bands[i] > s ? bands[i] : t32(s - (s - bands[i]) * fall));
        a.at = now;
    }
    return { level, levelSmoothed: a.levelSmoothed, bands, bandsSmoothed: a.bandsSmoothed };
}
const sphereOnBeat = f => f.levelSmoothed >= 8 && f.level > f.levelSmoothed + 8;
function sphereBands(f, from, to) { let sum = 0; for (let i = from; i <= to; ++i) sum += f.bandsSmoothed[i]; return t32(sum / (to - from + 1)); }
function sphereStepDt(mem, t, min) {
    let dt = t - mem.last;
    if (dt >= 0 && dt < min) return 0;
    if (dt < 0 || dt > 60) dt = 60;
    mem.last = t; return dt;
}
// Gradient noise at 8 bits, src/core/util/noise.h (Tier8, three dimensions).
const SPHERE_GRADIENT = [[1, 1, 0], [-1, 1, 0], [1, -1, 0], [-1, -1, 0], [1, 0, 1], [-1, 0, 1], [1, 0, -1], [-1, 0, -1],
    [0, 1, 1], [0, -1, 1], [0, 1, -1], [0, -1, -1], [1, 1, 0], [0, -1, 1], [-1, 1, 0], [0, -1, -1]];
const SPHERE_FADE = Array.from({ length: 257 }, (_v, i) => Math.floor((6 * i ** 5 - 3840 * i ** 4 + 655360 * i ** 3) / 16777216));
function sphereCorner(x, y, z) {
    let h = (Math.imul(x, 0x8da6b343) ^ Math.imul(y, 0xd8163841) ^ Math.imul(z, 0xcb1ab31f)) >>> 0;
    h = (h ^ (h >>> 16)) >>> 0; h = Math.imul(h, 0x7feb352d) >>> 0; return h >>> 28;
}
const sphereGrad = (h, x, y, z) => { const g = SPHERE_GRADIENT[h]; return g[0] * x + g[1] * y + g[2] * z; };
const sphereBlend = (a, b, w) => a + (((b - a) * w) >> 16);
function sphereLayer(ix, iy, iz, fx, fy, fz, wx, wy) {
    const v = sphereBlend(sphereGrad(sphereCorner(ix, iy, iz), fx, fy, fz), sphereGrad(sphereCorner(ix + 1, iy, iz), fx - 256, fy, fz), wx);
    return sphereBlend(v, sphereBlend(sphereGrad(sphereCorner(ix, iy + 1, iz), fx, fy - 256, fz),
        sphereGrad(sphereCorner(ix + 1, iy + 1, iz), fx - 256, fy - 256, fz), wx), wy);
}
function sphereNoise8(x, y, z) {
    const ix = x >>> 8, iy = y >>> 8, iz = z >>> 8, fx = x & 255, fy = y & 255, fz = z & 255;
    const wx = SPHERE_FADE[fx], wy = SPHERE_FADE[fy];
    const raw = sphereBlend(sphereLayer(ix, iy, iz, fx, fy, fz, wx, wy), sphereLayer(ix, iy, iz + 1, fx, fy, fz - 256, wx, wy), SPHERE_FADE[fz]);
    const v = 128 + (raw >> 1);
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

// Each draws one frame into `out` (count × [r,g,b]); `c(name)` reads a control within its firmware range.
const SPHERE_FRAMES = {
    SphereMeridianEffect(mem, t, n, c, pal, out) {
        mem.place ??= 0; mem.lag ??= 0; mem.last ??= 0;
        const speed = c('speed', 60, 1, 255), tail = c('tail', 60, 1, 255), size = c('size', 16, 2, 60);
        const dt = sphereStepDt(mem, t, 8);
        if (dt) {
            const sway = t32((sphereSine((t % 16384) * 4) - 32768) / 64);
            const moved = t32(dt * (sway + 160) * speed / 1536);
            mem.place = (mem.place + moved) & 65535;
            mem.lag = Math.max(-14000, Math.min(14000, mem.lag - moved));
            mem.lag -= t32(mem.lag * dt / (tail * 15 + 100));
        }
        const near = sphereBrightIndex(pal, t32(t / 128) & 255), far = sphereBrightIndex(pal, (t32(t / 128) + 128) & 255);
        const reach = size * 100, lag = mem.lag;
        for (let i = 0; i < n; ++i) {
            let s = (sphereAngleOf(i, n) - mem.place) & 65535, h = near;
            if (s >= 16384 && s < 49152) { s -= 32768; h = far; }
            if (s >= 49152) s -= 65536;
            let bri = 0;
            const distance = Math.abs(s);
            if (distance < reach) bri = 255 - t32(distance * 255 / reach);
            if (lag < 0 && s <= 0 && s > lag) bri = Math.max(bri, 255 - t32(s * 255 / lag));
            if (lag > 0 && s >= 0 && s < lag) bri = Math.max(bri, 255 - t32(s * 255 / lag));
            out[i] = sphereColor(pal, h, bri);
        }
    },
    SphereTideEffect(mem, t, n, c, pal, out) {
        const speed = c('speed', 60, 8, 255), reach = c('reach', 1, 0, 3), low = c('low', 60, 0, 255);
        const moonMs = t32(1560000 / speed), moon = spherePhaseOf(t, moonMs), sun = spherePhaseOf(t, moonMs * 4);
        let high = t32(sphereCosine(((moon - sun) & 65535) * 2) / 256);
        high = low + t32((255 - low) * high / 255);
        const near = sphereBrightIndex(pal, t32(t / 300) & 255), far = sphereBrightIndex(pal, (t32(t / 300) + 128) & 255);
        for (let i = 0; i < n; ++i) {
            const off = (sphereAngleOf(i, n) - moon) & 65535;
            let v = t32(sphereCosine(off * 2) / 256);
            v = t32(v * v / 255);
            for (let s = 0; s < reach; ++s) v = t32(v * v / 255);
            out[i] = sphereColor(pal, off > 16384 && off < 49152 ? far : near, t32(v * high / 255));
        }
    },
    SphereHarmonicsEffect(mem, t, n, c, pal, out) {
        const speed = c('speed', 100, 1, 255), drift = c('drift', 40, 0, 255), sharp = c('sharp', 1, 0, 3);
        const beatMs = 5200 - speed * 16, k = t32(t / beatMs) % 8, waves = k > 4 ? 10 - k : 2 + k;
        const swell = sphereSine(t32((t % (beatMs * 2)) * 32768 / beatMs)) - 32768;
        const spin = drift ? spherePhaseOf(t, Math.min(t32(2600000 / drift), 260000)) : 0;
        const hue = ((t32(t / beatMs) % 256) * 29) & 255;
        const near = sphereBrightIndex(pal, hue), far = sphereBrightIndex(pal, (hue + 128) & 255);
        for (let i = 0; i < n; ++i) {
            const along = ((sphereAngleOf(i, n) + spin) & 65535) * waves;
            let v = t32((sphereSine(along) - 32768) / 256) * t32(swell / 256), h = near;
            if (v < 0) { v = -v; h = far; }
            let bri = Math.min(t32(v / 64), 255);
            for (let s = 0; s < sharp; ++s) bri = t32(bri * bri / 255);
            out[i] = sphereColor(pal, h, bri);
        }
    },
    SphereKaleidoBloomEffect(mem, t, n, c, pal, out) {
        const speed = c('speed', 100, 1, 255), veins = c('veins', 3, 0, 8), drift = c('drift', 30, 0, 255);
        const bloomMs = 9000 - speed * 22, k = t32(t / bloomMs) % 252, count = [3, 4, 6, 5, 8, 6, 4][k % 7];
        let open = t32((65535 - sphereCosine(t32((t % bloomMs) * 65536 / bloomMs))) / 2);
        open = Math.min(open * 3, 65535);
        const spin = drift ? spherePhaseOf(t, Math.min(t32(4000000 / drift), 260000)) : 0;
        const crawl = (t % 8192) * 8, near = sphereBrightIndex(pal, (k * 37) & 255);
        for (let i = 0; i < n; ++i) {
            let p = ((((sphereAngleOf(i, n) + spin) & 65535) * count) & 65535) * 2;
            if (p > 65534) p = 131070 - p;
            let bri = 0;
            if (p > 65535 - open) {
                bri = t32(t32((p - 65535 + open) / 64) * 255 / (t32(open / 64) + 1));
                bri = Math.min(bri * 2, 255);
                const vein = t32(sphereSine(t32(p * veins / 2) - crawl) / 624);
                if (veins > 0) bri = t32(bri * (150 + vein) / 255);
            }
            out[i] = sphereColor(pal, (near + t32(p / 2800)) & 255, bri);
        }
    },
    SphereDayAndNightEffect(mem, t, n, c, _pal, out) {
        const day = c('day', 30, 4, 240), lights = c('lights', 120, 0, 255), dusk = c('dusk', 40, 10, 120);
        const sun = spherePhaseOf(t, day * 1000), edge = dusk * 200;
        for (let i = 0; i < n; ++i) {
            const a = sphereAngleOf(i, n), high = sphereCosine((a - sun) & 65535) - 32768;
            let rr = 0, gg = 0, bb = 0;
            if (high > edge) {
                const k = t32((high - edge) * 255 / (32768 - edge));
                rr = 255; gg = 150 + t32(k * 80 / 255); bb = 50 + t32(k * 130 / 255);
            } else if (high > -edge) {
                const k = t32((high + edge) * 255 / (2 * edge));
                if (k < 128) { rr = 6 + t32(k * 249 / 128); gg = 4 + t32(k * 36 / 128); bb = 36 - t32(k * 26 / 128); }
                else { rr = 255; gg = 40 + t32((k - 128) * 110 / 127); bb = 10 + t32((k - 128) * 40 / 127); }
            } else {
                const deep = Math.min(t32((-edge - high) / 64), 255);
                rr = 0; gg = 2; bb = 14 + t32(deep / 12);
                const x = a * 4, town = sphereNoise8(x, 0, 0) + lights - 255;
                if (town > 0 && deep > 20) {
                    let glow = t32(Math.min(town * 3, 255) * (130 + t32(sphereNoise8(x, t32(t / 3), 9000) / 2)) / 255);
                    glow = t32(glow * Math.min(deep * 4, 255) / 255);
                    rr += glow; gg += t32(glow * 170 / 255); bb += t32(glow * 60 / 255);
                }
            }
            out[i] = [sphereLevel(rr), sphereLevel(gg), sphereLevel(bb)];
        }
    },
    SphereOrbitExchangeEffect(mem, t, n, c, pal, out) {
        const speed = c('speed', 120, 1, 255), tail = c('tail', 110, 5, 200), flash = c('flash', 200, 0, 255);
        const passMs = 5200 - speed * 18, k = t32(t / passMs) % 256, since = t32((t % passMs) * 32768 / passMs);
        const meet = (spherePhaseOf(t, 240000) + (k % 2 === 1 ? 32768 : 0)) & 65535;
        const hueOf = pass => (pass * 23 + (pass % 2 ? 128 : 0)) & 255, len = tail * 100;
        const near = sphereBrightIndex(pal, hueOf(k)), nearBefore = sphereBrightIndex(pal, hueOf(k + 255));
        const far = sphereBrightIndex(pal, (hueOf(k) + 128) & 255), farBefore = sphereBrightIndex(pal, (hueOf(k + 255) + 128) & 255);
        const tailOf = (behind, now, before) => {
            const v = behind < len ? 255 - t32(behind * 255 / len) : 0;
            return sphereColor(pal, behind <= since ? now : before, t32(v * v / 255));
        };
        for (let i = 0; i < n; ++i) {
            const a = sphereAngleOf(i, n);
            const one = tailOf((meet + since - a) & 65535, near, nearBefore), two = tailOf((a - meet + since) & 65535, far, farBefore);
            let rr = one[0] + two[0], gg = one[1] + two[1], bb = one[2] + two[2];
            if (since < 9000) {
                let d = ((a - meet) & 65535) % 32768;
                if (d > 16384) d = 32768 - d;
                if (d < 900 + since) {
                    let v = t32(t32((255 - t32(d * 255 / (900 + since))) * (9000 - since) / 9000) * flash / 255);
                    if (((a - meet + 16384) & 65535) >= 32768) v = t32(v / 3);
                    rr += v; gg += v; bb += v;
                }
            }
            out[i] = [sphereLevel(rr), sphereLevel(gg), sphereLevel(bb)];
        }
    },
    SphereAntipodeDropEffect(mem, t, n, c, pal, out, f) {
        const speed = c('speed', 110, 1, 255), rate = c('rate', 150, 0, 255), wake = c('wake', 70, 5, 120);
        const onBeat = controlValue(mem.effect, 'onBeat', true) !== false;
        mem.drops ??= Array.from({ length: 12 }, () => ({ origin: 0, born: 0, tint: 0 }));
        mem.nextAt ??= 0; mem.lastAt ??= 0; mem.seed ??= 0x2545F491;
        const life = 5400 - speed * 16;
        for (const d of mem.drops) if (d.born && t - (d.born - 1) >= life) d.born = 0;
        if (t >= mem.nextAt || t < mem.lastAt || (onBeat && sphereOnBeat(f) && t - mem.lastAt > 220)) {
            const d = mem.drops.find(slot => !slot.born);
            if (d) {
                d.tint = sphereBrightIndex(pal, (sphereRandom(mem) >> 8) & 255);
                d.origin = sphereRandom(mem) & 0xff00;
                d.born = t + 1; mem.lastAt = t;
                mem.nextAt = t + Math.min(500 + (255 - rate) * 14, t32(life * 3 / 5));
            }
        }
        const wakeLength = wake * 100;
        for (let i = 0; i < n; ++i) {
            const a = sphereAngleOf(i, n);
            let rr = 0, gg = 0, bb = 0;
            for (const drop of mem.drops) {
                if (!drop.born) continue;
                const age = t - (drop.born - 1), run = Math.floor(age * 65536 / life);
                const left = 255 - t32(age * 255 / life), fade = t32(left * left / 255);
                for (const ahead of [(a - drop.origin) & 65535, (drop.origin - a) & 65535]) {
                    const behind = run - ahead;
                    if (behind < 0 || behind >= wakeLength) continue;
                    const w = sphereColor(pal, drop.tint, t32((255 - t32(behind * 255 / wakeLength)) * fade / 255));
                    rr += w[0]; gg += w[1]; bb += w[2];
                }
                let d = (a - drop.origin) & 65535;
                if (d > 32768) d = 65536 - d;
                const landing = 255 - t32(run * 255 / 9000);
                if (landing > 0 && d < 1200) { const v = t32(t32(landing * (1200 - d) / 1200) * fade / 255); rr += v; gg += v; bb += v; }
                const meeting = 255 - t32(Math.abs(run - 32768) * 255 / 7000);
                if (meeting > 0 && d > 32768 - 1500) { const v = t32(t32(meeting * (d - 31268) / 1500) * fade / 255); rr += v; gg += v; bb += v; }
            }
            out[i] = [sphereLevel(rr), sphereLevel(gg), sphereLevel(bb)];
        }
    },
    SphereBassBreathEffect(mem, t, n, c, pal, out, f) {
        const gain = c('gain', 170, 10, 255), floor = c('floor', 5, 0, 100), release = c('release', 40, 1, 255), turn = c('turn', 20, 0, 255);
        mem.breath ??= new SphereEnvelope(); mem.peak ??= 14 << 8; mem.last ??= 0;
        const dt = sphereStepDt(mem, t, 10);
        if (dt) {
            const bass = sphereBands(f, 0, 9);
            mem.peak = Math.max(bass << 8, mem.peak - t32(mem.peak * dt / 4000), (floor + 9) << 8);
            const heard = t32(Math.max(0, bass - floor) * 255 / ((mem.peak >> 8) - floor));
            mem.breath.step(t32(heard * gain / 100), dt, 90, t32(Math.max(1, 256 - release) / 16) + 1);
        }
        const lvl = mem.breath.level(), open = t32(lvl * 16384 / 255), glow = Math.min(lvl * 3, 255);
        const axis = turn ? spherePhaseOf(t, Math.min(t32(4000000 / turn), 260000)) : 0;
        const hue = sphereBrightIndex(pal, t32(t / 500) & 255);
        for (let i = 0; i < n; ++i) {
            const d = sphereFromAxis(sphereAngleOf(i, n), axis);
            let v = 0;
            if (d < open) v = 255 - t32(d * 160 / (open + 1));
            else if (d < open + 2400) v = 95 - t32((d - open) * 95 / 2400);
            out[i] = sphereColor(pal, (hue + t32(d / 512)) & 255, t32(v * glow / 255));
        }
    },
    SphereBeatEchoEffect(mem, t, n, c, pal, out, f) {
        const speed = c('speed', 110, 1, 255), width = c('width', 60, 10, 160), glow = c('glow', 30, 0, 120);
        mem.echoes ??= Array.from({ length: 10 }, () => ({ born: 0, tint: 0, strength: 0 }));
        mem.glow ??= new SphereEnvelope(); mem.last ??= 0; mem.lastEcho ??= 0; mem.count ??= 0; mem.wasBeat ??= false;
        const travel = 3200 - speed * 10;
        for (const e of mem.echoes) if (e.born && t - (e.born - 1) >= travel) e.born = 0;
        const dt = sphereStepDt(mem, t, 10);
        if (dt) {
            mem.glow.step(sphereBands(f, 0, 15) * 2, dt, 40, 8);
            const heard = sphereOnBeat(f);
            if (heard && !mem.wasBeat && t - mem.lastEcho > 180) {
                const e = mem.echoes.find(slot => !slot.born);
                if (e) {
                    e.born = t + 1; e.tint = sphereBrightIndex(pal, (mem.count * 41) & 255);
                    e.strength = Math.max(140, Math.min(255, 140 + sphereBands(f, 0, 3)));
                    mem.count = (mem.count + 1) & 255; mem.lastEcho = t;
                }
            }
            mem.wasBeat = heard;
        }
        const half = width * 40, base = t32(mem.glow.level() * glow / 255), baseHue = sphereBrightIndex(pal, t32(t / 700) & 255);
        for (let i = 0; i < n; ++i) {
            const d = sphereFromAxis(sphereAngleOf(i, n), 0), under = sphereColor(pal, (baseHue + t32(d / 256)) & 255, base);
            let rr = under[0], gg = under[1], bb = under[2];
            for (const e of mem.echoes) {
                if (!e.born) continue;
                const age = t - (e.born - 1), at = Math.floor(age * 16384 / travel), off = Math.abs(d - at);
                if (off >= half) continue;
                const left = 255 - t32(age * 255 / travel), swell = Math.min(t32(age * 255 / 90), 255), shape = 255 - t32(off * 255 / half);
                const col = sphereColor(pal, e.tint, t32(t32(t32(t32(shape * shape / 255) * left / 255) * swell / 255) * e.strength / 255));
                rr += col[0]; gg += col[1]; bb += col[2];
            }
            out[i] = [sphereLevel(rr), sphereLevel(gg), sphereLevel(bb)];
        }
    },
    SphereMirrorSparksEffect(mem, t, n, c, pal, out, f) {
        const density = c('density', 120, 0, 255), decay = c('decay', 60, 1, 255), glow = c('glow', 40, 0, 160);
        mem.sparks ??= Array.from({ length: 24 }, () => ({ at: 0, born: 0, tint: 0 }));
        mem.bass ??= new SphereEnvelope(); mem.owed ??= 0; mem.last ??= 0; mem.seed ??= 0x6C8E9CF5;
        const life = 300 + decay * 12;
        for (const s of mem.sparks) if (s.born && t - (s.born - 1) >= life) s.born = 0;
        const dt = sphereStepDt(mem, t, 10);
        if (dt) {
            mem.bass.step(sphereBands(f, 0, 3) * 2, dt, 60, 10);
            mem.owed += t32(sphereBands(f, 9, 15) * density * dt / 10);
            while (mem.owed >= 65536) {
                mem.owed -= 65536;
                const s = mem.sparks.find(slot => !slot.born);
                if (!s) { mem.owed = 0; break; }
                s.at = sphereRandom(mem) % 16385; s.tint = sphereBrightIndex(pal, (sphereRandom(mem) >> 8) & 255); s.born = t + 1;
            }
        }
        const base = t32(mem.bass.level() * glow / 160), baseHue = sphereBrightIndex(pal, t32(t / 900) & 255);
        for (let i = 0; i < n; ++i) {
            const d = sphereFromAxis(sphereAngleOf(i, n), 0), under = sphereColor(pal, baseHue, t32(base * Math.max(0, 8000 - d) / 8000));
            let rr = under[0], gg = under[1], bb = under[2];
            for (const s of mem.sparks) {
                if (!s.born) continue;
                const off = Math.abs(d - s.at);
                if (off >= 500) continue;
                const age = t - (s.born - 1), swell = Math.min(t32(age * 255 / 60), 255), left = 255 - t32(age * 255 / life);
                const col = sphereColor(pal, s.tint, t32(t32(t32(t32((500 - off) * 255 / 500) * swell / 255) * left / 255) * left / 255));
                rr += col[0]; gg += col[1]; bb += col[2];
            }
            out[i] = [sphereLevel(rr), sphereLevel(gg), sphereLevel(bb)];
        }
    },
    SphereSpectrumHaloEffect(mem, t, n, c, pal, out, f) {
        const gain = c('gain', 97, 10, 255), fall = c('fall', 34, 1, 255), idle = c('idle', 0, 0, 80), kick = c('kick', 32, 0, 255);
        mem.levels ??= new Array(16).fill(0); mem.axis ??= 0; mem.last ??= 0; mem.hit ??= 0; mem.wasBeat ??= false;
        const dt = sphereStepDt(mem, t, 10);
        if (dt) {
            const heard = sphereOnBeat(f);
            if (heard && !mem.wasBeat) { mem.hit = 255; mem.axis = (mem.axis + kick * 24) & 65535; }
            mem.wasBeat = heard;
            mem.hit = Math.max(mem.hit - dt * 2, 0);
            mem.axis = (mem.axis + dt * 3) & 65535;
            for (let i = 0; i < 16; ++i) mem.levels[i] = Math.max(Math.min(t32(f.bands[i] * gain / 100), 255), mem.levels[i] - t32(dt * fall / 32)) & 255;
        }
        const near = sphereBrightIndex(pal, t32(t / 400) & 255);
        for (let i = 0; i < n; ++i) {
            const d = sphereFromAxis(sphereAngleOf(i, n), mem.axis), band = t32(d * 15 / 16385), part = t32(d * 15 / 64) - band * 256;
            let v = t32((mem.levels[band] * (256 - part) + mem.levels[Math.min(band + 1, 15)] * part) / 256);
            v = Math.max(t32(v * v / 255), idle);
            const col = sphereColor(pal, (near + t32(d / 256)) & 255, v), white = d < 2600 ? t32(mem.hit * (2600 - d) / 2600) : 0;
            out[i] = [sphereLevel(col[0] + white), sphereLevel(col[1] + white), sphereLevel(col[2] + white)];
        }
    },
};
const sphereStates = new WeakMap();
export function spherePreviewColor(effect, pal, t, x, count, audio = HELIO_PREVIEW_AUDIO) {
    const draw = SPHERE_FRAMES[effect?.type];
    if (!draw) return [0, 0, 0];
    const lights = Math.min(4096, Math.max(1, Math.round(Number(count) || HELIO_DEFAULT_LED_COUNT)));
    const now = Math.floor(Math.max(0, Number(t) || 0) * 1000 + 1e-7) >>> 0;
    let state = sphereStates.get(effect);
    // A preview that starts again (time running back, another light count) starts the effect again, as a fresh one would.
    if (!state || now < state.now || state.lights !== lights) {
        state = { now: -1, lights, mem: { effect }, frame: Array.from({ length: lights }, () => [0, 0, 0]) };
        sphereStates.set(effect, state);
    }
    if (state.now !== now) {
        const c = (name, fallback, min, max) => {
            const v = Number(controlValue(effect, name, fallback));
            return Math.min(max, Math.max(min, Number.isFinite(v) ? Math.trunc(v) : fallback));
        };
        draw(state.mem, now, lights, c, pal, state.frame, sphereAudio(state.mem, now, audio));
        state.now = now;
    }
    const pixel = Math.min(lights - 1, Math.floor(Math.max(0, Math.min(0.999999, Number(x) || 0)) * lights));
    return [...state.frame[pixel]];
}

export function helioScenePreviewColor(effect, pal, t, x, y, audio = HELIO_PREVIEW_AUDIO, opts = {}) {
    if (typeof effect?.type === "string" && effect.type.startsWith("Sphere"))
        return spherePreviewColor(effect, pal, t, x, opts.ledCount, audio);
    if (effect?.type === "NebuliteNoisefireEffect")
        return nebuliteNoisefirePreviewColor(effect, t, x, opts.ledCount, audio, opts);
    if (effect?.type === "NebulitePlasmoidEffect")
        return nebulitePlasmoidPreviewColor(effect, t, x, opts.ledCount, audio, opts);
    if (effect?.type === "NebuliteFlowStripeEffect")
        return nebuliteFlowStripePreviewColor(effect, t, x, opts.ledCount);
    if (effect?.type === "NebuliteSnowflakesEffect")
        return nebuliteSnowflakesPreviewColor(effect, t, x, opts.ledCount, opts);
    if (effect?.type === "NebuliteRainbowEffect")
        return nebuliteRainbowPreviewColor(effect, t, x, opts.ledCount);
    if (effect?.type === "NebuliteBlinkEffect")
        return nebuliteBlinkPreviewColor(effect, t, x, opts.ledCount, opts);
    if (effect?.type === "NebuliteSolidEffect")
        return nebuliteSolidPreviewColor(effect, x, opts.ledCount, opts);
    if (effect?.type === "NebuliteWashingEffect")
        return nebuliteWashingPreviewColor(effect, t, x, opts.ledCount);
    if (effect?.type === "NebuliteSineEffect")
        return nebuliteSinePreviewColor(effect, t, x, opts.ledCount, opts);
    if (effect?.type === "NebulitePaletteEffect")
        return nebulitePalettePreviewColor(effect, t, x, opts.ledCount);
    if (effect?.type === "NebuliteFireflyEffect")
        return nebuliteFireflyPreviewColor(effect, t, x, opts.ledCount, opts);
    return helioPresetPreviewColor(helioScenePresetIndex(effect, t), effect, pal, t, x, y, audio, opts);
}
