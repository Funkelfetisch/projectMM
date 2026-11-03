// Approximate 30-LED garment simulation using the historical jacket artwork.
// Each sample remains in controller output order; the artwork is not a
// measurement of the garment or a model of its optical response.
let assets = null;
const listeners = new Set();
const ready = image => image && image.complete && image.naturalWidth > 0;
function images() {
    if (assets || typeof Image === 'undefined') return assets;
    const texture = new Image(), garment = new Image();
    assets = {texture, garment};
    const loaded = () => {
        if (!ready(texture) || !ready(garment)) return;
        for (const listener of listeners) listener();
        listeners.clear();
    };
    texture.onload = garment.onload = loaded;
    texture.onerror = garment.onerror = () => listeners.clear();
    texture.src = '/products/jacket_leds.png';
    garment.src = '/products/jacket_top.png';
    return assets;
}
export function whenJacketPreviewReady(listener) {
    const art = images();
    if (!art || typeof listener !== 'function') return;
    if (!ready(art.texture) || !ready(art.garment)) listeners.add(listener);
}
const byte = value => Math.max(0, Math.min(255, Math.round(Number(value) || 0)));
export function drawJacketPreview(canvas, time, sampler, projection, art = images()) {
    const ctx = canvas?.getContext?.('2d'), w = canvas?.width || 0, h = canvas?.height || 0;
    const points = projection?.points;
    if (!ctx || !w || !h || typeof sampler !== 'function' || !Array.isArray(points) || points.length !== 31 ||
        !ready(art?.texture) || !ready(art?.garment)) return false;
    ctx.fillStyle = '#05070d'; ctx.fillRect(0, 0, w, h);
    const side = Math.min(w, h);
    ctx.save(); ctx.translate((w - side) / 2, (h - side) / 2); ctx.scale(side / 200, side / 200);
    // The point coordinates also feed the controller's PixelMapLayout. The
    // transform positions that same schematic on the front-view illustration.
    for (let i = 0; i < 30; ++i) {
        const [x, y] = points[i];
        const c = sampler((i + .5) / points.length, .5, time) || [];
        const colour = `${byte(c[0])}, ${byte(c[1])}, ${byte(c[2])}`;
        const px = 12 + x * 5.8, py = 25 + y * 10;
        const glow = ctx.createRadialGradient(px, py, 0, px, py, 24);
        glow.addColorStop(0, `rgba(${colour}, 0.85)`);
        glow.addColorStop(1, `rgba(${colour}, 0)`);
        ctx.fillStyle = glow; ctx.fillRect(px - 24, py - 24, 48, 48);
    }
    ctx.globalCompositeOperation = 'destination-in'; ctx.drawImage(art.texture, 0, 0, 200, 200);
    ctx.globalCompositeOperation = 'multiply'; ctx.drawImage(art.texture, 0, 0, 200, 200);
    ctx.globalCompositeOperation = 'source-over'; ctx.drawImage(art.garment, 0, 0, 200, 200);
    ctx.restore();
    canvas.dataset.previewRenderer = 'jacket-30-simulation';
    canvas.setAttribute('aria-label', 'Approximate 30 LED jacket animation; independent white output not shown');
    return true;
}
