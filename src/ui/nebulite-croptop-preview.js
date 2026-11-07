// Historical WLED crop-top illustration; source overlay SHA-256:
// 36de7ad2271cc466db2c52d7d2c26ec86c6f89028236ea46d2d128afd0f00d00.
// The overlay's one-based references are not a physically verified mapping.
const SHAPES = [{"c1":5,"c2":1,"points":[[69.1,82.0],[66.7,107.787],[64.3,133.5],[53.0,133.063],[41.7,132.7],[43.9,107.0],[46.1,81.3],[48.311,81.978],[53.82,83.072],[57.1,83.3],[60.691,83.336],[66.747,82.583],[69.1,82.0],[69.1,82.0]],"gradient":[69.1,82.0,41.7,132.7],"stops":[[0.2,"c1"],[0.8,"c2"]]},{"c1":4,"c2":2,"points":[[166.1,97.7],[166.1,98.5],[166.4,99.6],[166.4,99.762],[166.4,100.6],[166.25,102.225],[166.1,103.1],[165.6,104.2],[164.6,105.1],[164.3,105.4],[164.1,109.5],[163.0,114.6],[162.9,133.6],[163.8,132.8],[164.7,125.4],[164.4,121.1],[164.4,117.8],[164.9,115.5],[166.1,107.9],[167.4,101.4],[167.4,99.0],[166.4,97.3],[166.1,97.7],[166.1,97.7]],"gradient":[166.1,97.7,162.9,133.6],"stops":[[0.2,"c1"],[0.8,"c2"]]},{"c1":4,"c2":1,"points":[[35.5,97.4],[38.9,89.9],[41.1,87.1],[43.8,83.6],[46.1,81.3],[41.7,132.7],[35.0,132.1],[35.5,108.8],[35.1,107.6],[34.6,107.0],[34.2,106.4],[33.7,105.1],[33.1,101.8],[33.1,100.9],[33.1,100.0],[33.8,98.7],[35.1,98.0]],"gradient":[35.5,97.4,41.7,132.7],"stops":[[0.2,"c1"],[0.8,"c2"]]},{"c1":6,"c2":1,"points":[[72.6,133.1],[73.0,132.8],[73.1,131.1],[73.9,129.4],[74.5,125.7],[74.3,121.7],[75.5,117.5],[76.0,111.8],[78.2,104.1],[77.6,101.9],[77.1,100.6],[76.8,99.1],[76.0,97.1],[75.1,95.4],[74.5,94.3],[73.8,93.2],[72.8,91.2],[72.0,88.9],[70.5,84.1],[70.2,82.6],[69.1,82.0],[64.3,133.6],[71.1,133.6]],"gradient":[72.6,133.1,69.1,82.0],"stops":[[0.2,"c1"],[0.8,"c2"]]},{"c1":3,"c2":2,"points":[[118.7,104.4],[120.5,115.4],[121.9,123.5],[122.5,128.0],[123.3,130.2],[123.5,131.3],[124.0,132.0],[124.7,132.3],[125.8,132.6],[127.9,132.8],[129.8,132.8],[131.4,132.5],[136.3,131.4],[136.1,130.6],[134.9,127.9],[133.9,124.5],[133.9,121.6],[132.9,114.9],[132.4,112.4],[131.5,109.7],[131.4,108.7],[128.7,108.8],[127.0,108.7],[125.2,108.3],[122.5,107.4],[120.7,106.2],[120.3,105.5]],"gradient":[118.7,104.4,136.3,131.4],"stops":[[0.2,"c1"],[0.8,"c2"]]},{"c1":7,"c2":8,"points":[[141.7,109.9],[141.6,111.1],[141.3,113.7],[141.5,115.8],[141.3,121.8],[141.5,126.6],[142.0,131.7],[142.1,132.8],[142.9,133.2],[144.1,133.6],[145.9,134.0],[148.1,134.2],[151.1,134.3],[153.3,134.5],[155.5,134.5],[158.7,134.3],[161.1,134.1],[162.8,133.6],[163.0,131.9],[162.7,123.6],[162.9,114.6],[164.0,109.5],[164.2,105.4],[163.2,106.3],[161.7,107.2],[157.8,108.3],[155.4,108.7],[152.5,109.1],[149.8,109.1],[146.8,109.1],[143.9,109.3],[143.0,109.3]],"gradient":[141.7,109.9,162.8,133.6],"stops":[[0.2,"c1"],[0.8,"c2"]]}];
let cachedAssets = null;
const readyListeners = new Set();
function images() {
    if (cachedAssets || typeof Image === "undefined") return cachedAssets;
    const texture = new Image(), garment = new Image();
    cachedAssets = { texture, garment };
    const loaded = () => {
        if (!ready(texture) || !ready(garment)) return;
        for (const listener of readyListeners) listener();
        readyListeners.clear();
    };
    texture.onload = garment.onload = loaded;
    texture.onerror = garment.onerror = () => readyListeners.clear();
    texture.src = "/products/croptop_leds.png";
    garment.src = "/products/croptop_top.png";
    return cachedAssets;
}
const ready = image => image && image.complete && image.naturalWidth > 0;
// One listener per caller; a failed image leaves the existing strip fallback in place.
export function whenHistoricalCroptopReady(listener) {
    const assets = images();
    if (!assets || typeof listener !== "function") return;
    if (!ready(assets.texture) || !ready(assets.garment)) readyListeners.add(listener);
}
const byte = value => Math.max(0, Math.min(255, Math.round(Number(value) || 0)));
const rgb = color => `rgb(${byte(color?.[0])}, ${byte(color?.[1])}, ${byte(color?.[2])})`;

// The original WLED overlay blends adjacent RGB pixels into illustrated fabric.
export function drawHistoricalCroptop(canvas, time, sampler, logicalCount, assets = images()) {
    const ctx = canvas?.getContext?.("2d"), w = canvas?.width || 0, h = canvas?.height || 0;
    if (!ctx || !w || !h || typeof sampler !== "function" || logicalCount !== 8 ||
        !ready(assets?.texture) || !ready(assets?.garment)) return false;
    const colors = Array.from({ length: 8 }, (_, i) => rgb(sampler((i + 0.5) / logicalCount, 0.5, time)));
    ctx.fillStyle = "#000";
    ctx.fillRect(0, 0, w, h);
    const side = Math.min(w, h);
    ctx.save();
    ctx.translate((w - side) / 2, (h - side) / 2);
    ctx.scale(side / 200, side / 200);
    for (const shape of SHAPES) {
        const gradient = ctx.createLinearGradient(...shape.gradient);
        for (const [offset, channel] of shape.stops)
            gradient.addColorStop(offset, colors[(channel === "c1" ? shape.c1 : shape.c2) - 1]);
        ctx.beginPath();
        ctx.moveTo(...shape.points[0]);
        for (const point of shape.points.slice(1)) ctx.lineTo(...point);
        ctx.closePath();
        ctx.fillStyle = gradient;
        ctx.fill();
    }
    ctx.globalCompositeOperation = "hard-light";
    ctx.drawImage(assets.texture, 0, 0, 200, 200);
    ctx.globalCompositeOperation = "source-over";
    ctx.drawImage(assets.garment, 0, 0, 200, 200);
    ctx.restore();
    canvas.dataset.previewRenderer = "historical-croptop-simulation";
    canvas.dataset.previewSource = "historical-simulation";
    canvas.setAttribute("aria-label", "Approximate historical crop-top animation simulation");
    return true;
}
