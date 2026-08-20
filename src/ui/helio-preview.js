import {
    HELIO_DEFAULT_LED_COUNT,
    HELIO_SCENE_PLAYLISTS,
    HELIO_SCENE_PRESETS,
    helioFactorySceneForType,
} from "./helio-scenes.js?ui=20260820a";

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
        let color = [0, 0, 0];
        const count = Math.max(1, Math.min(8, clampByte(controlValue(effect, "count", 4))));
        const ringSpeed = clampByte(controlValue(effect, "speed", 42));
        const thickness = Math.max(0.012, clampByte(controlValue(effect, "thickness", 5)) / 50);
        const hueShift = clampByte(controlValue(effect, "hue_shift", 0));
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

export function helioScenePreviewColor(effect, pal, t, x, y, audio = HELIO_PREVIEW_AUDIO, opts = {}) {
    return helioPresetPreviewColor(helioScenePresetIndex(effect, t), effect, pal, t, x, y, audio, opts);
}
