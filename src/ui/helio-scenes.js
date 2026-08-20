const HELIO_MEASURED_DIAMETER_MM = 700;
const HELIO_MEASURED_LED_COUNT = 311;
const HELIO_EDGE_MARGIN_MM = 8;
const HELIO_PRODUCT_SIZES = [
    [30, 131],
    [50, 222],
    [70, 311],
    [100, 446],
    [150, 671],
    [200, 895],
];

export const HELIO_DEFAULT_LED_COUNT = 222;

export function helioLedCountForDiameterCm(diameterCm) {
    const diameter = Math.max(0.1, Number(diameterCm) || HELIO_MEASURED_DIAMETER_MM / 10);
    const product = HELIO_PRODUCT_SIZES.find(([candidate]) => candidate === diameter);
    if (product) return product[1];
    const diameterMm = diameter * 10;
    const measuredPathMm = HELIO_MEASURED_DIAMETER_MM - HELIO_EDGE_MARGIN_MM;
    const pathMm = Math.max(1, diameterMm - HELIO_EDGE_MARGIN_MM);
    return Math.max(1, Math.round(HELIO_MEASURED_LED_COUNT * pathMm / measuredPathMm));
}

export const HELIO_SIZE_PRESETS = HELIO_PRODUCT_SIZES.map(([diameterCm, count]) => ({
    label: `${diameterCm} cm`,
    count,
}));

const HELIO_PALETTE = Object.freeze({
    rainbow: 0,
    analogous: 12,
    aquaFlash: 14,
    candy: 21,
    hult: 28,
    orangery: 36,
    splash: 50,
    tiamat: 53,
    yelbluHot: 56,
});

export const HELIO_FACTORY_SCENES = [
    { id: "candle-multi", label: "Candle Glow", type: "HelioCandleMultiEffect", meta: "warm flickering light", presetIndex: 0, source: { preset: 1, effect: 102, palette: 0 }, speed: 53, intensity: 255, palette: HELIO_PALETTE.rainbow, primary: [255, 133, 0], white: 255, secondary: [0, 0, 0], controls: { speed: 53, "effect intensity": 255, palette: HELIO_PALETTE.rainbow, red: 255, green: 133, blue: 0, white: 255, output: 255 } },
    { id: "colortwinkles", label: "Color Sparkles", type: "HelioColorTwinklesEffect", meta: "soft sparse sparkle", presetIndex: 1, source: { preset: 2, effect: 74, palette: 28 }, speed: 128, intensity: 128, palette: HELIO_PALETTE.hult, primary: [255, 133, 0], white: 255, secondary: [0, 0, 0], controls: { speed: 128, "effect intensity": 128, palette: HELIO_PALETTE.hult, red: 255, green: 133, blue: 0, white: 255, output: 255 } },
    { id: "freqmap-amber", label: "Amber Bands", type: "HelioFreqMapAmberEffect", meta: "slow amber drift", presetIndex: 2, source: { preset: 3, effect: 155, palette: 11 }, speed: 128, intensity: 128, palette: HELIO_PALETTE.rainbow, primary: [255, 133, 0], white: 255, secondary: [0, 0, 0], controls: { speed: 128, "effect intensity": 128, palette: HELIO_PALETTE.rainbow, red: 255, green: 133, blue: 0, white: 255, output: 255 } },
    { id: "lake", label: "Lake", type: "HelioLakeEffect", meta: "liquid color wash", presetIndex: 3, source: { preset: 4, effect: 75, palette: 0 }, speed: 128, intensity: 128, palette: HELIO_PALETTE.rainbow, primary: [255, 133, 0], white: 255, secondary: [0, 0, 0], controls: { speed: 128, "effect intensity": 128, palette: HELIO_PALETTE.rainbow, red: 255, green: 133, blue: 0, white: 255, output: 255 } },
    { id: "loading", label: "Orbit", type: "HelioLoadingEffect", meta: "warm moving trace", presetIndex: 4, source: { preset: 5, effect: 47, palette: 18 }, speed: 128, intensity: 16, palette: HELIO_PALETTE.analogous, primary: [255, 133, 0], white: 255, secondary: [0, 0, 0], controls: { speed: 128, "effect intensity": 16, palette: HELIO_PALETTE.analogous, red: 255, green: 133, blue: 0, white: 255, output: 255 } },
    { id: "pink-palette", label: "Pink Glow", type: "HelioPinkPaletteEffect", meta: "pink color field", presetIndex: 5, source: { preset: 6, effect: 65, palette: 19 }, speed: 128, intensity: 128, palette: HELIO_PALETTE.splash, primary: [255, 133, 0], white: 255, secondary: [0, 0, 0], controls: { speed: 128, "effect intensity": 128, palette: HELIO_PALETTE.splash, red: 255, green: 133, blue: 0, white: 255, output: 255 } },
    { id: "phased", label: "Phase Shimmer", type: "HelioPhasedEffect", meta: "interference shimmer", presetIndex: 6, source: { preset: 7, effect: 105, palette: 45 }, speed: 128, intensity: 128, palette: HELIO_PALETTE.tiamat, primary: [255, 133, 0], white: 255, secondary: [0, 0, 0], controls: { speed: 128, "effect intensity": 128, palette: HELIO_PALETTE.tiamat, red: 255, green: 133, blue: 0, white: 255, output: 255 } },
    { id: "ripple", label: "Ripple", type: "RingsEffect", meta: "expanding reflected arcs", presetIndex: 7, source: { preset: 8, effect: 79, palette: 0 }, palette: HELIO_PALETTE.rainbow, controls: { count: 4, speed: 42, thickness: 5, hue_shift: 0 } },
    { id: "running", label: "Chase", type: "HelioRunningEffect", meta: "linear moving light", presetIndex: 8, source: { preset: 9, effect: 15, palette: 73 }, speed: 135, intensity: 128, palette: HELIO_PALETTE.orangery, primary: [255, 133, 0], white: 0, secondary: [0, 0, 0], controls: { speed: 135, "effect intensity": 128, palette: HELIO_PALETTE.orangery, red: 255, green: 133, blue: 0, white: 0, output: 255 } },
    { id: "starburst", label: "Starburst", type: "HelioStarburstEffect", meta: "audio burst rings", presetIndex: 9, audio: true, source: { preset: 12, effect: 192, palette: 57 }, speed: 75, intensity: 128, palette: HELIO_PALETTE.candy, primary: [255, 133, 0], white: 0, secondary: [0, 0, 0], controls: { speed: 75, "effect intensity": 128, palette: HELIO_PALETTE.candy, red: 255, green: 133, blue: 0, white: 0, output: 255 } },
    { id: "gravcenter", label: "Gravity Center", type: "HelioGravCenterEffect", meta: "audio center pull", presetIndex: 10, audio: true, source: { preset: 13, effect: 156, palette: 11 }, speed: 128, intensity: 128, palette: HELIO_PALETTE.rainbow, primary: [255, 133, 0], white: 0, secondary: [0, 0, 0], controls: { speed: 128, "effect intensity": 128, palette: HELIO_PALETTE.rainbow, red: 255, green: 133, blue: 0, white: 0, output: 255 } },
    { id: "juggles", label: "Juggling Dots", type: "HelioJugglesEffect", meta: "audio bouncing dots", presetIndex: 11, audio: true, source: { preset: 14, effect: 130, palette: 64 }, speed: 128, intensity: 255, palette: HELIO_PALETTE.yelbluHot, primary: [255, 133, 0], white: 0, secondary: [0, 0, 0], controls: { speed: 128, "effect intensity": 255, palette: HELIO_PALETTE.yelbluHot, red: 255, green: 133, blue: 0, white: 0, output: 255 } },
    { id: "multi-comet", label: "Comets", type: "HelioMultiCometEffect", meta: "audio comet swarm", presetIndex: 12, audio: true, source: { preset: 15, effect: 191, palette: 64 }, speed: 91, intensity: 7, palette: HELIO_PALETTE.yelbluHot, primary: [255, 133, 0], white: 0, secondary: [0, 0, 0], controls: { speed: 91, "effect intensity": 7, palette: HELIO_PALETTE.yelbluHot, red: 255, green: 133, blue: 0, white: 0, output: 255 } },
    { id: "noisemeter", label: "Level Meter", type: "HelioNoiseMeterEffect", meta: "audio level glow", presetIndex: 13, audio: true, source: { preset: 16, effect: 136, palette: 64 }, speed: 248, intensity: 128, palette: HELIO_PALETTE.yelbluHot, primary: [255, 135, 0], white: 0, secondary: [0, 0, 0], controls: { speed: 248, "effect intensity": 128, palette: HELIO_PALETTE.yelbluHot, red: 255, green: 135, blue: 0, white: 0, output: 255 } },
    { id: "plasmoid", label: "Plasmoid", type: "HelioPlasmoidEffect", meta: "audio plasma field", presetIndex: 14, audio: true, source: { preset: 17, effect: 133, palette: 28 }, speed: 128, intensity: 80, palette: HELIO_PALETTE.hult, primary: [255, 135, 0], white: 0, secondary: [0, 0, 0], controls: { speed: 128, "effect intensity": 80, palette: HELIO_PALETTE.hult, red: 255, green: 135, blue: 0, white: 0, output: 255 } },
    { id: "freqmap-wide", label: "Wide Spectrum", type: "HelioFreqMapWideEffect", meta: "audio frequency sweep", presetIndex: 15, audio: true, source: { preset: 18, effect: 155, palette: 63 }, speed: 192, intensity: 128, palette: HELIO_PALETTE.aquaFlash, primary: [255, 135, 0], white: 0, secondary: [0, 0, 0], controls: { speed: 192, "effect intensity": 128, palette: HELIO_PALETTE.aquaFlash, red: 255, green: 135, blue: 0, white: 0, "option 1": true, output: 255 } },
    { id: "geq", label: "Equalizer", type: "HelioGEQEffect", meta: "audio spectrum bars", presetIndex: 16, audio: true, source: { preset: 19, effect: 139, palette: 11 }, speed: 128, intensity: 128, palette: HELIO_PALETTE.rainbow, primary: [255, 135, 0], white: 0, secondary: [111, 255, 0], controls: { speed: 128, "effect intensity": 128, palette: HELIO_PALETTE.rainbow, red: 255, green: 135, blue: 0, "secondary red": 111, "secondary green": 255, "secondary blue": 0, "custom 1": 255, "custom 2": 64, white: 0, output: 255 } },
    { id: "gravfreq", label: "Gravity Bands", type: "HelioGravFreqEffect", meta: "audio frequency pull", presetIndex: 17, audio: true, source: { preset: 20, effect: 158, palette: 1 }, speed: 71, intensity: 98, palette: HELIO_PALETTE.orangery, primary: [255, 135, 0], white: 0, secondary: [111, 255, 0], controls: { speed: 71, "effect intensity": 98, palette: HELIO_PALETTE.orangery, red: 255, green: 135, blue: 0, "secondary red": 111, "secondary green": 255, "secondary blue": 0, white: 0, output: 255 } },
    { id: "freqwave", label: "Frequency Wave", type: "HelioFreqWaveEffect", meta: "audio wave bands", presetIndex: 18, audio: true, source: { preset: 21, effect: 137, palette: 1 }, speed: 202, intensity: 247, palette: HELIO_PALETTE.rainbow, primary: [0, 0, 255], white: 0, secondary: [111, 255, 0], controls: { speed: 202, "effect intensity": 247, palette: HELIO_PALETTE.rainbow, red: 0, green: 0, blue: 255, "secondary red": 111, "secondary green": 255, "secondary blue": 0, "custom 1": 9, "custom 2": 255, "custom 3": 9, white: 0, output: 255 } },
    { id: "orbiting-lobes", label: "Orbiting Lobes", type: "LavaLampEffect", meta: "slow reflected crescents", presetIndex: 19, palette: HELIO_PALETTE.orangery, controls: { bpm: 7, radius: 24, intensity: 190 } },
    { id: "harmonic-halo", label: "Harmonic Halo", type: "DistortionWavesEffect", meta: "breathing mirrored meridians", presetIndex: 20, palette: HELIO_PALETTE.aquaFlash, controls: { freq_x: 2, freq_y: 3, speed: 28 } },
    { id: "liquid-mirror", label: "Liquid Mirror", type: "PlasmaEffect", meta: "flowing reflected color bands", presetIndex: 21, palette: HELIO_PALETTE.tiamat, controls: { bpm: 14, scale_x: 96, scale_y: 112, hue_shift: 8 } },
    { id: "twin-suns", label: "Twin Suns", type: "MetaballsEffect", meta: "paired orbiting highlights", presetIndex: 22, palette: HELIO_PALETTE.candy, controls: { bpm: 10, radius: 20, count: 2, hue_shift: 24 } },
];

export const HELIO_MODES = HELIO_FACTORY_SCENES;
export const HELIO_SCENE_PRESETS = HELIO_FACTORY_SCENES;

export const HELIO_SCENE_PLAYLISTS = [
    { id: "static", label: "Static Animations", value: 0, sourcePreset: 10, duration: 10, transition: 0.7, repeat: true, order: [7, 3, 6, 8, 0, 4, 1, 5] },
    { id: "audio", label: "Audio Animations", value: 1, sourcePreset: 11, duration: 30, transition: 2, repeat: true, order: [16, 17, 13, 15, 18, 11, 10, 12, 14, 9] },
    { id: "sphere", label: "Sphere Optics", value: 2, duration: 18, transition: 1.2, repeat: true, order: [7, 19, 20, 21, 22] },
];

export function helioFactorySceneForType(type) {
    return HELIO_FACTORY_SCENES.find(scene => scene.type === type) || null;
}

export function isHelioPortEffectType(type) {
    const name = String(type || "");
    return name === "HelioWledPresetsEffect" || (/^Helio.*Effect$/.test(name) && !isNativeHelioSceneType(name));
}

export function isNativeHelioSceneType(type) {
    return HELIO_FACTORY_SCENES.some(scene => scene.type === String(type || ""));
}

export function helioPreferredActiveEffect(effects) {
    const modules = Array.isArray(effects) ? effects.filter(Boolean) : [];
    const enabled = modules.filter(effect => effect.enabled !== false);
    return enabled.find(effect => isNativeHelioSceneType(effect.type)) ||
        enabled.find(effect => !isHelioPortEffectType(effect.type)) ||
        enabled[0] ||
        null;
}

export function helioSceneForPresetIndex(index) {
    return HELIO_FACTORY_SCENES.find(scene => scene.presetIndex === Number(index)) || null;
}

export function helioPlaylistFirstScene(playlistValue) {
    const playlist = HELIO_SCENE_PLAYLISTS.find(pl => pl.value === Number(playlistValue)) || HELIO_SCENE_PLAYLISTS[0];
    return helioSceneForPresetIndex(playlist.order[0]);
}

export function helioSceneUsesAudio(scene) {
    const resolved = typeof scene === "number" ? helioSceneForPresetIndex(scene) : scene;
    return resolved?.audio === true;
}

export function helioPlaylistUsesAudio(playlist) {
    return (playlist?.order || []).some(presetIndex => helioSceneUsesAudio(presetIndex));
}
