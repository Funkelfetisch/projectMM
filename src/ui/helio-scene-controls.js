import { HELIO_FACTORY_SCENES } from "./helio-scene-data.js";

function sceneFor(scene) {
    return typeof scene === "string"
        ? HELIO_FACTORY_SCENES.find(candidate => candidate.id === scene || candidate.type === scene)
        : scene;
}

// The shared catalog owns effect-specific labels and help; other controls keep their normal labels.
export function helioSceneControlMetadata(scene, name) {
    const metadata = sceneFor(scene)?.controlMetadata?.[name];
    return {
        label: metadata?.label || null,
        description: metadata?.description || "",
        visible: metadata?.visible !== false,
    };
}

export function helioSceneControlLabel(scene, name, fallback = name) {
    return helioSceneControlMetadata(scene, name).label || fallback;
}

export function helioSceneControlDescription(scene, name) {
    return helioSceneControlMetadata(scene, name).description;
}

export function helioSceneControlVisible(scene, name) {
    return helioSceneControlMetadata(scene, name).visible;
}
