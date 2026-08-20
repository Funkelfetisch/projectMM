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

export function helioAudioStartEnabled(audioModule) {
    if (!audioModule || audioModule.enabled === false) return false;
    const value = (name, fallback = -1) => {
        const ctrl = (audioModule.controls || []).find(candidate => candidate?.name === name);
        const n = Number(ctrl?.value);
        return Number.isFinite(n) ? n : fallback;
    };
    return value("sckPin") >= 0 && value("wsPin") >= 0 && value("sdPin") >= 0;
}
