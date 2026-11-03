// The preview stream carries pre-driver RGB source values, not measured LEDs.
// Accept only a full, evenly ordered one-dimensional source table so a garment
// illustration never silently assigns a coarsened or reordered frame to LEDs.
export function nebuliteLiveSampler(frame, logicalCount, gridWidth = logicalCount) {
    if (!frame || frame.stride !== 1 || frame.count !== logicalCount || logicalCount < 2 ||
        frame.rgb?.length !== logicalCount * 3 || frame.coords?.length !== logicalCount * 3) return null;
    const coords = frame.coords;
    if (Array.isArray(gridWidth)) {
        const points = gridWidth;
        if (points.length !== logicalCount || points.some(p => !Array.isArray(p) || p.length !== 2 || !p.every(Number.isFinite))) return null;
        const anchor = points[0];
        const ref = points.findIndex(p => p[0] !== anchor[0] || p[1] !== anchor[1]);
        const axis = ref >= 0 && points[ref][0] === anchor[0] ? 1 : 0;
        const step = ref < 0 ? 0 : (coords[ref * 3 + axis] - coords[axis]) / (points[ref][axis] - anchor[axis]);
        if (!Number.isFinite(step) || (ref >= 0 && step <= 0)) return null;
        for (let i = 0; i < logicalCount; ++i) {
            if (!Number.isFinite(coords[i * 3]) || !Number.isFinite(coords[i * 3 + 1]) || !Number.isFinite(coords[i * 3 + 2]) ||
                Math.abs(coords[i * 3] - (coords[0] + (points[i][0] - anchor[0]) * step)) > 0.0001 ||
                Math.abs(coords[i * 3 + 1] - (coords[1] + (points[i][1] - anchor[1]) * step)) > 0.0001 ||
                Math.abs(coords[i * 3 + 2] - coords[2]) > 0.0001) return null;
        }
        return x => {
            const i = Math.min(logicalCount - 1, Math.max(0, Math.floor(x * logicalCount))) * 3;
            return [frame.rgb[i], frame.rgb[i + 1], frame.rgb[i + 2]];
        };
    }
    if (!Number.isInteger(gridWidth) || gridWidth < 2 || logicalCount % gridWidth) return null;
    const step = coords[3] - coords[0];
    const rows = logicalCount / gridWidth;
    const yStep = rows > 1 ? coords[gridWidth * 3 + 1] - coords[1] : 0;
    if (!Number.isFinite(step) || step <= 0 || (rows > 1 && (!Number.isFinite(yStep) || yStep <= 0))) return null;
    for (let i = 0; i < logicalCount; ++i) {
        if (!Number.isFinite(coords[i * 3]) || !Number.isFinite(coords[i * 3 + 1]) ||
            !Number.isFinite(coords[i * 3 + 2]) ||
            Math.abs(coords[i * 3] - (coords[0] + (i % gridWidth) * step)) > 0.0001 ||
            Math.abs(coords[i * 3 + 1] - (coords[1] + Math.floor(i / gridWidth) * yStep)) > 0.0001 ||
            Math.abs(coords[i * 3 + 2] - coords[2]) > 0.0001) return null;
    }
    return x => {
        const i = Math.min(logicalCount - 1, Math.max(0, Math.floor(x * logicalCount))) * 3;
        return [frame.rgb[i], frame.rgb[i + 1], frame.rgb[i + 2]];
    };
}

