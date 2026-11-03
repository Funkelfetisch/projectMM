// Current MoonLight persists selected profiles/palettes by name; older controls
// still return numeric rows. Resolve both without turning an unknown name into 0.
export function helioChoiceIndex(ctrl) {
    const value = ctrl?.value ?? 0;
    if (typeof value !== "string") return value;
    return (ctrl?.options || []).findIndex(option =>
        (typeof option === "string" ? option : option?.name) === value);
}

export function planHelioEffectActivation(effects, targetName, controls = {}, opts = {}) {
    const modules = Array.isArray(effects) ? effects : [];
    const target = modules.find(effect => effect && effect.name === targetName);
    if (!target?.name) return [];

    const controlEquals = opts.controlEquals || ((ctrl, next) => ctrl?.value === next);
    const controlValueForSend = opts.controlValueForSend || ((_ctrl, value) => value);
    const operations = [];

    for (const effect of modules) {
        if (effect?.name && effect.name !== target.name && effect.enabled !== false) {
            operations.push({ module: effect.name, control: "enabled", value: false });
        }
    }

    for (const [name, value] of Object.entries(controls || {})) {
        const ctrl = (target.controls || []).find(candidate => candidate?.name === name);
        if (!ctrl) continue;
        const next = controlValueForSend(ctrl, value);
        if (!controlEquals(ctrl, next)) {
            operations.push({ module: target.name, control: name, value: next });
        }
    }

    if (target.enabled === false) {
        operations.push({ module: target.name, control: "enabled", value: true });
    }
    return operations;
}

// A scene transition is one configuration document, so its controls share one
// server preparation instead of one HTTP round trip and preparation per knob.
export function planHelioControlDocument(modules, operations) {
    const paths = new Map();
    const visit = (items, parent = []) => {
        for (const item of items || []) {
            const path = [...parent, item.name];
            paths.set(item.name, path);
            visit(item.children, path);
        }
    };
    visit(modules);
    const document = {};
    for (const operation of operations || []) {
        const path = paths.get(operation.module);
        if (!path) throw new Error(`Unknown module: ${operation.module}`);
        let target = document;
        for (const name of path) {
            if (!Object.hasOwn(target, name)) Object.defineProperty(target, name,
                { value: {}, enumerable: true, writable: true, configurable: true });
            target = target[name];
        }
        Object.defineProperty(target, operation.control,
            { value: operation.value, enumerable: true, writable: true, configurable: true });
    }
    return document;
}

export function helioAudioStartEnabled(audioModule) {
    if (!audioModule || audioModule.enabled === false) return false;
    const value = (name, fallback = -1) => {
        const ctrl = (audioModule.controls || []).find(candidate => candidate?.name === name);
        const n = Number(ctrl?.value);
        return Number.isFinite(n) ? n : fallback;
    };
    return value("sckPin") >= 0 && value("wsPin") >= 0 && value("sdPin") >= 0;
}
