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



export { HELIO_FACTORY_SCENES } from "./helio-scene-data.js";
import { HELIO_FACTORY_SCENES } from "./helio-scene-data.js";

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
    return name === "HelioWledPresetsEffect" || (/^Helio.*Effect$/.test(name) &&
        !!helioFactorySceneForType(name)?.source?.effect);
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
