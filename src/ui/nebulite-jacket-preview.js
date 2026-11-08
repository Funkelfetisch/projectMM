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
// The 2021 jacket as the NEBULITE app draws it (funkelapp-jacket.json): each garment panel runs between two outputs, [first, second, x1, y1, x2, y2] on the artwork.
const JACKET_50_PANELS = [[9,17,91,79,127,178],[12,17,97,105,123,183],[27,37,91,75,130,183],[28,35,50,84,20,165],[31,36,47,137,16,174],[27,37,56,78,8,160],[10,3,99,174,97,132],[11,2,97,128,104,147],[6,5,70,85,85,173],[7,4,87,82,99,174],[29,22,50,84,48,134],[25,24,63,85,50,174],[26,23,55,81,50,174],[47,38,69,41,63,85],[42,41,71,30,105,43],[43,40,71,30,37,40],[44,42,71,30,87,82],[45,39,71,30,58,83],[46,43,70,35,79,86],[47,42,126,11,132,53],[49,43,141,26,134,55],[44,40,117,12,106,54],[45,41,117,55,119,11],[28,35,145,54,204,110],[31,36,124,54,192,124],[32,37,125,54,204,110],[29,22,142,94,147,122],[30,21,130,63,148,138],[33,20,122,54,119,116],[34,19,117,55,114,104],[11,2,110,54,91,75],[14,1,110,54,107,87],[15,0,111,54,111,96],[13,18,110,54,92,65]];
const byte = value => Math.max(0, Math.min(255, Math.round(Number(value) || 0)));
export function drawJacketPreview(canvas, time, sampler, projection, art = images()) {
    const ctx = canvas?.getContext?.('2d'), w = canvas?.width || 0, h = canvas?.height || 0;
    const points = projection?.points;
    if (!ctx || !w || !h || typeof sampler !== 'function' || !Array.isArray(points) || (points.length !== 30 && points.length !== 50) ||
        !ready(art?.texture) || !ready(art?.garment)) return false;
    ctx.fillStyle = '#05070d'; ctx.fillRect(0, 0, w, h);
    const side = Math.min(w, h);
    ctx.save(); ctx.translate((w - side) / 2, (h - side) / 2); ctx.scale(side / 200, side / 200);
    const glow = (px, py, c) => {
        const colour = `${byte(c[0])}, ${byte(c[1])}, ${byte(c[2])}`;
        const light = ctx.createRadialGradient(px, py, 0, px, py, 24);
        light.addColorStop(0, `rgba(${colour}, 0.85)`);
        light.addColorStop(1, `rgba(${colour}, 0)`);
        ctx.fillStyle = light; ctx.fillRect(px - 24, py - 24, 48, 48);
    };
    // The point coordinates also feed the controller's PixelMapLayout; the artwork shows the outputs at its own positions.
    const colours = points.map((_, i) => sampler((i + .5) / points.length, .5, time) || []);
    if (points.length === 50) {
        for (const [first, second, x1, y1, x2, y2] of JACKET_50_PANELS) {
            glow(x1, y1, colours[first]);
            glow((x1 + x2) / 2, (y1 + y2) / 2, colours[first].map((v, c) => (v + (colours[second][c] || 0)) / 2));
            glow(x2, y2, colours[second]);
        }
    } else points.forEach(([x, y], i) => glow(12 + x * 5.8, 25 + y * 10, colours[i]));
    ctx.globalCompositeOperation = 'destination-in'; ctx.drawImage(art.texture, 0, 0, 200, 200);
    ctx.globalCompositeOperation = 'multiply'; ctx.drawImage(art.texture, 0, 0, 200, 200);
    ctx.globalCompositeOperation = 'source-over'; ctx.drawImage(art.garment, 0, 0, 200, 200);
    ctx.restore();
    canvas.dataset.previewRenderer = 'jacket-30-simulation';
    canvas.setAttribute('aria-label', `Approximate ${points.length} LED jacket animation`);
    return true;
}
