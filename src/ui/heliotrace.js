// HelioTrace: HELIO's mirrored single-strip ball renderer.
//
// This is the firmware-friendly version of the helio.lighting raymarcher. It
// keeps the actual optical model from the live renderer: an analytical acrylic
// shell, a single equator LED texture with package/gap subpixels, warm-white
// dichroic handling, recursive internal bounces, and the black equatorial cuff.
// It intentionally avoids Three.js so the controller UI still works when the
// browser is connected directly to the HELIO access point with no internet.

const TAU = Math.PI * 2;
const LEDS_PER_METER = 144;
const LED_DIAMETER_MM = 5.0;
const LED_PITCH_MM = 1000 / LEDS_PER_METER;
const DEFAULT_WALL_MM = 3.0;
const SCALE = 0.01;
const WW = [1.0, 0.72, 0.45];

function clampByte(v) {
    return Math.max(0, Math.min(255, Math.round(Number(v) || 0)));
}

function clamp(v, lo, hi) {
    return Math.max(lo, Math.min(hi, v));
}

function resizeCanvas(canvas, maxDpr = 1.5) {
    const dpr = Math.min(maxDpr, window.devicePixelRatio || 1);
    const rect = canvas.getBoundingClientRect();
    const w = Math.max(1, Math.round(rect.width * dpr));
    const h = Math.max(1, Math.round(rect.height * dpr));
    if (canvas.width !== w || canvas.height !== h) {
        canvas.width = w;
        canvas.height = h;
        return true;
    }
    return false;
}

function shader(gl, type, src) {
    const s = gl.createShader(type);
    gl.shaderSource(s, src);
    gl.compileShader(s);
    if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) {
        const msg = gl.getShaderInfoLog(s) || "shader compile failed";
        gl.deleteShader(s);
        throw new Error(msg);
    }
    return s;
}

function program(gl, vs, fs) {
    const p = gl.createProgram();
    const v = shader(gl, gl.VERTEX_SHADER, vs);
    const f = shader(gl, gl.FRAGMENT_SHADER, fs);
    gl.attachShader(p, v);
    gl.attachShader(p, f);
    gl.linkProgram(p);
    gl.deleteShader(v);
    gl.deleteShader(f);
    if (!gl.getProgramParameter(p, gl.LINK_STATUS)) {
        const msg = gl.getProgramInfoLog(p) || "program link failed";
        gl.deleteProgram(p);
        throw new Error(msg);
    }
    return p;
}

function fallbackSample(x, t) {
    const r = 0.5 + 0.5 * Math.sin((x + t * 0.03) * TAU);
    const g = 0.5 + 0.5 * Math.sin((x + t * 0.03 + 0.33) * TAU);
    const b = 0.5 + 0.5 * Math.sin((x + t * 0.03 + 0.66) * TAU);
    return [r * 255, g * 255, b * 255];
}

function normalize(v) {
    const d = Math.hypot(v[0], v[1], v[2]) || 1;
    return [v[0] / d, v[1] / d, v[2] / d];
}

function cross(a, b) {
    return [
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    ];
}

function cameraBasis(pos, target) {
    const f = normalize([target[0] - pos[0], target[1] - pos[1], target[2] - pos[2]]);
    let r = normalize(cross(f, [0, 1, 0]));
    if (!Number.isFinite(r[0])) r = [1, 0, 0];
    const u = normalize(cross(r, f));
    return { r, u, f };
}

function inferDiameterMm(ledCount) {
    const count = Math.max(1, Number(ledCount) || 311);
    const pathRadiusMm = (count / LEDS_PER_METER) * 1000 / TAU;
    return Math.max(120, (pathRadiusMm + DEFAULT_WALL_MM + 1.0) * 2);
}

const VERT = `
attribute vec2 a_pos;
varying vec2 vUv;
void main() {
    vUv = a_pos * 0.5 + 0.5;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}`;

const FRAG = `
precision highp float;
varying vec2 vUv;
uniform vec3 uCamPos, uCamR, uCamU, uCamF;
uniform float uTanFov, uAspect;
uniform float uInnerR, uOuterR, uCapE, uRimR, uFlangeOuter, uFlangeHalf, uWallT;
uniform float uBounces, uAtten, uCutoff, uBandHalf, uExposure, uDichroic, uGlass, uShowRim;
uniform float uDiUpA, uDiUpB, uDiLoA, uDiLoB, uDiEdge, uDiBounce;
uniform sampler2D uLeds;
#define PI 3.14159265359

vec3 ledColorAt(float az){
    float u = fract(az / (2.0 * PI) + 1.0);
    return texture2D(uLeds, vec2(u, 0.5)).rgb;
}

bool hitCap(vec3 ro, vec3 rd, float cy, float Rr, float ySign, out float tHit, out vec3 nrm){
    vec3 oc = ro - vec3(0.0, cy, 0.0);
    float b = dot(oc, rd);
    float c = dot(oc, oc) - Rr * Rr;
    float d = b * b - c;
    if(d < 0.0) return false;
    float s = sqrt(d);
    for(int j = 0; j < 2; j++){
        float tt = (j == 0) ? (-b - s) : (-b + s);
        if(tt > 1e-4){
            vec3 p = ro + rd * tt;
            if(p.y * ySign >= -1e-5){
                tHit = tt;
                nrm = (p - vec3(0.0, cy, 0.0)) / Rr;
                return true;
            }
        }
    }
    return false;
}

bool nextCapHit(vec3 ro, vec3 rd, float Rr, out float tHit, out vec3 nrm){
    float tu, tl; vec3 nu, nl;
    bool hu = hitCap(ro, rd, -uCapE, Rr,  1.0, tu, nu);
    bool hl = hitCap(ro, rd,  uCapE, Rr, -1.0, tl, nl);
    if(hu && (!hl || tu <= tl)){ tHit = tu; nrm = nu; return true; }
    if(hl){ tHit = tl; nrm = nl; return true; }
    return false;
}

vec3 hue2rgb(float h){
    h = fract(h);
    vec3 k = abs(mod(h * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0;
    return clamp(k, 0.0, 1.0);
}

float mixHue(float a, float b, float t){
    float d = b - a;
    d -= floor(d + 0.5);
    return a + d * t;
}

vec3 ledEmit(vec3 c, float phase, float dStrength){
    const vec3 WW = vec3(1.0, 0.72, 0.45);
    float W = min(min(c.r / WW.r, c.g / WW.g), c.b / WW.b);
    vec3 chromatic = max(c - W * WW, vec3(0.0));
    vec3 hue = hue2rgb(phase);
    float hl = dot(hue, vec3(0.299, 0.587, 0.114));
    hue *= clamp(0.6 / max(hl, 0.05), 0.7, 3.5);
    float ds = clamp(dStrength, 0.0, 1.0);
    vec3 chrom = chromatic * (1.0 - 0.7 * ds);
    vec3 white = W * mix(WW, hue, ds);
    return chrom + white;
}

vec3 traceTunnel(vec3 P, vec3 dir, float exitPhase, float dStr){
    float tp = 1.0;
    vec3 col = vec3(0.0);
    int maxB = int(uBounces);
    {
        float cy0 = (P.y >= 0.0) ? -uCapE : uCapE;
        vec3 n0 = normalize(P - vec3(0.0, cy0, 0.0));
        float face = max(0.0, dot(dir, n0));
        if(abs(P.y) < uBandHalf && face > 0.0){
            float az = atan(P.z, P.x);
            float dy = abs(P.y) / uBandHalf;
            col += tp * ledEmit(ledColorAt(az), exitPhase, dStr) * (1.0 - dy * dy) * face;
        }
    }
    for(int k = 0; k < 64; k++){
        if(k >= maxB) break;
        float t; vec3 nrm;
        if(!nextCapHit(P, dir, uInnerR, t, nrm)) break;
        vec3 Pn = P + dir * t;
        float face = max(0.0, dot(dir, nrm));
        if(abs(Pn.y) < uBandHalf && face > 0.0){
            float az = atan(Pn.z, Pn.x);
            float dy = abs(Pn.y) / uBandHalf;
            float fall = 1.0 - dy * dy;
            float phase = exitPhase + float(k + 1) * uDiBounce;
            col += tp * ledEmit(ledColorAt(az), phase, dStr) * fall * face;
        }
        tp *= uAtten;
        if(tp < uCutoff) break;
        dir = reflect(dir, nrm);
        P = Pn;
    }
    return col;
}

vec3 aces(vec3 x) {
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

void main(){
    vec2 ndc = vUv * 2.0 - 1.0;
    vec3 rd = normalize(uCamF + uCamR * (ndc.x * uAspect * uTanFov) + uCamU * (ndc.y * uTanFov));
    vec3 ro = uCamPos;

    vec3 outCol = vec3(0.0);
    bool any = false;
    float sphereT = 1e9;

    float oT; vec3 nO;
    bool hitOuter = nextCapHit(ro, rd, uOuterR, oT, nO) && oT > 0.0;
    if(hitOuter){
        any = true;
        sphereT = oT;
        vec3 pO = ro + rd * oT;
        float fres = pow(1.0 - abs(dot(nO, rd)), 3.0);
        vec3 glass = vec3(0.05, 0.14, 0.11) * fres * uGlass;

        vec3 rdR = refract(rd, nO, 1.0 / 1.49);
        if(dot(rdR, rdR) < 1e-4) rdR = rd;
        vec3 rdIn = normalize(mix(rd, rdR, uGlass));

        float iT; vec3 nI;
        if(nextCapHit(pO, rdIn, uInnerR, iT, nI)){
            vec3 P = pO + rdIn * iT;
            float cosI = abs(dot(nO, rd));
            float raw = 1.0 - cosI;
            float tint = smoothstep(0.0, uDiEdge, raw);
            float exitPhase = (nO.y > 0.0)
                ? mixHue(uDiUpA, uDiUpB, tint)
                : mixHue(uDiLoA, uDiLoB, tint);
            float dStr = clamp(uDichroic, 0.0, 1.0) * mix(0.15, 1.0, tint);
            outCol = traceTunnel(P, rdIn, exitPhase, dStr) + glass;
        } else {
            outCol = glass;
        }
    }

    if(uShowRim > 0.5 && abs(rd.y) > 1e-6){
        float h = uFlangeHalf;
        float tTop = (h - ro.y) / rd.y;
        float tBot = (-h - ro.y) / rd.y;
        float tIn = max(min(tTop, tBot), 0.0);
        float tOut = max(tTop, tBot);
        if(tOut > 0.0){
            vec3 mid = ro + rd * (0.5 * (tIn + tOut));
            float fr = length(mid.xz);
            if(fr >= uRimR && fr <= uFlangeOuter){
                any = true;
                float az = atan(mid.z, mid.x);
                float brush = 0.85 + 0.15 * sin(az * 240.0);
                vec3 fn = vec3(0.0, rd.y > 0.0 ? -1.0 : 1.0, 0.0);
                float ff = pow(1.0 - abs(dot(fn, rd)), 4.0);
                vec3 metal = vec3(0.010, 0.010, 0.012) * brush;
                metal += vec3(0.03, 0.033, 0.04) * ff;
                if(tIn < sphereT) outCol = metal;
            }
        }
    }

    if(!any){
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec3 mapped = aces(outCol * uExposure);
    mapped = pow(mapped, vec3(1.0 / 2.2));
    gl_FragColor = vec4(mapped, 1.0);
}`;

export class HelioTrace {
    constructor(opts = {}) {
        this.canvas = null;
        this.gl = null;
        this.program = null;
        this.texture = null;
        this.buffer = null;
        this.sampler = fallbackSample;
        this.ledCount = 311;
        this.diameterMm = inferDiameterMm(this.ledCount);
        this.wallMm = DEFAULT_WALL_MM;
        this.cutoffDeg = 180;
        this.exposure = 2.5;
        this.bounces = opts.bounces || 24;
        this.maxDpr = opts.maxDpr || 1.45;
        this.interactive = !!opts.interactive;
        this.autoSpin = opts.autoSpin !== false;
        this.running = false;
        this.raf = 0;
        this.subpixels = 10;
        this.data = new Uint8Array(1);
        this.yaw = 0.868;
        this.pitch = 0.39;
        this.distance = 3.72;
        this.drag = null;
        this.reducedMotion = window.matchMedia &&
            window.matchMedia("(prefers-reduced-motion: reduce)").matches;
    }

    mount(canvas) {
        if (this.canvas === canvas && this.gl) return true;
        this.stop();
        this.canvas = canvas;
        try {
            this.initGl();
            if (this.interactive) this.bindPointerControls();
            this.draw(performance.now());
            return true;
        } catch (err) {
            console.debug("[heliotrace] WebGL unavailable:", err && err.message ? err.message : err);
            this.gl = null;
            this.drawFallback(performance.now());
            return false;
        }
    }

    initGl() {
        const gl = this.canvas.getContext("webgl", {
            antialias: true,
            alpha: false,
            depth: false,
            stencil: false,
            powerPreference: "high-performance",
            preserveDrawingBuffer: false,
        });
        if (!gl) throw new Error("webgl context unavailable");
        this.gl = gl;
        this.program = program(gl, VERT, FRAG);
        this.buffer = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, this.buffer);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
            -1, -1,  1, -1, -1,  1,
            -1,  1,  1, -1,  1,  1,
        ]), gl.STATIC_DRAW);

        this.texture = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, this.texture);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.REPEAT);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        this.ensureTextureStorage();
    }

    bindPointerControls() {
        if (this.canvas._helioTraceControls) return;
        this.canvas._helioTraceControls = true;
        this.canvas.addEventListener("pointerdown", (ev) => {
            this.drag = { x: ev.clientX, y: ev.clientY, yaw: this.yaw, pitch: this.pitch };
            this.canvas.setPointerCapture(ev.pointerId);
        });
        this.canvas.addEventListener("pointermove", (ev) => {
            if (!this.drag) return;
            this.yaw = this.drag.yaw + (ev.clientX - this.drag.x) * 0.008;
            this.pitch = clamp(this.drag.pitch + (ev.clientY - this.drag.y) * 0.006, -0.95, 0.95);
            this.draw(performance.now());
        });
        const end = () => { this.drag = null; };
        this.canvas.addEventListener("pointerup", end);
        this.canvas.addEventListener("pointercancel", end);
        this.canvas.addEventListener("wheel", (ev) => {
            ev.preventDefault();
            this.distance = clamp(this.distance * (1 + ev.deltaY * 0.0012), 2.35, 6.2);
            this.draw(performance.now());
        }, { passive: false });
    }

    setSampler(fn) {
        this.sampler = typeof fn === "function" ? fn : fallbackSample;
    }

    setLedCount(n) {
        const count = Math.max(1, Math.min(1200, Math.round(Number(n) || 311)));
        if (count === this.ledCount) return;
        this.ledCount = count;
        this.diameterMm = inferDiameterMm(count);
        this.ensureTextureStorage();
    }

    setExposure(v) {
        this.exposure = Math.max(0.5, Math.min(7.0, Number(v) || 2.5));
    }

    setDetail(opts = {}) {
        if (opts.bounces !== undefined) this.bounces = clamp(Number(opts.bounces) || this.bounces, 4, 40);
        if (opts.maxDpr !== undefined) this.maxDpr = clamp(Number(opts.maxDpr) || this.maxDpr, 0.65, 2.0);
        if (opts.autoSpin !== undefined) this.autoSpin = !!opts.autoSpin;
    }

    ensureTextureStorage() {
        if (!this.gl) return;
        const maxTex = this.gl.getParameter(this.gl.MAX_TEXTURE_SIZE) || 4096;
        const wanted = 14;
        this.subpixels = Math.max(3, Math.min(wanted, Math.floor(maxTex / Math.max(1, this.ledCount))));
        this.data = new Uint8Array(this.ledCount * this.subpixels * 4);
    }

    geometry() {
        const R = (this.diameterMm / 2) * SCALE;
        const wall = this.wallMm * SCALE;
        const inner = R - wall;
        const half = (this.cutoffDeg * Math.PI / 180) * 0.5;
        let capRo = R;
        let capE = 0;
        if (this.cutoffDeg < 179.5) {
            const s = Math.sin(half);
            capRo = R / Math.max(0.001, s);
            capE = R * Math.cos(half) / Math.max(0.001, s);
        }
        return {
            R,
            wall,
            capRo,
            capRi: capRo - wall,
            capE,
            flangeOuter: R + 50 * SCALE,
            ledPathR: inner - 1.0 * SCALE,
        };
    }

    camera(now, geom) {
        const spin = this.autoSpin && !this.drag && !this.reducedMotion ? now * 0.000035 : 0;
        const yaw = this.yaw + spin;
        const pitch = this.pitch;
        const d = geom.R * this.distance;
        const cp = Math.cos(pitch);
        const pos = [
            Math.cos(yaw) * cp * d,
            Math.sin(pitch) * d,
            Math.sin(yaw) * cp * d,
        ];
        return { pos, ...cameraBasis(pos, [0, 0, 0]) };
    }

    upload(t) {
        const gl = this.gl;
        const sub = this.subpixels;
        const halfPkg = (LED_DIAMETER_MM / LED_PITCH_MM) * 0.5;
        for (let i = 0; i < this.ledCount; i++) {
            const c = this.sampler(i / this.ledCount, 0.5, t) || [0, 0, 0];
            const r = clampByte(c[0]) / 255;
            const g = clampByte(c[1]) / 255;
            const b = clampByte(c[2]) / 255;
            const base = i * sub * 4;
            for (let s = 0; s < sub; s++) {
                const p = (s + 0.5) / sub - 0.5;
                const d = Math.abs(p) / halfPkg;
                const fall = d < 1.0 ? (1.0 - d * d) : 0.0;
                const o = base + s * 4;
                this.data[o] = clampByte(r * fall * 255);
                this.data[o + 1] = clampByte(g * fall * 255);
                this.data[o + 2] = clampByte(b * fall * 255);
                this.data[o + 3] = 255;
            }
        }
        gl.bindTexture(gl.TEXTURE_2D, this.texture);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, this.ledCount * sub, 1, 0,
                      gl.RGBA, gl.UNSIGNED_BYTE, this.data);
    }

    start() {
        if (this.running) return;
        this.running = true;
        const loop = (now) => {
            if (!this.running) return;
            this.draw(now);
            this.raf = requestAnimationFrame(loop);
        };
        this.raf = requestAnimationFrame(loop);
    }

    stop() {
        this.running = false;
        if (this.raf) cancelAnimationFrame(this.raf);
        this.raf = 0;
    }

    draw(now) {
        if (!this.canvas) return;
        if (!this.gl) {
            this.drawFallback(now);
            return;
        }
        const gl = this.gl;
        resizeCanvas(this.canvas, this.maxDpr);
        gl.viewport(0, 0, this.canvas.width, this.canvas.height);
        gl.clearColor(0, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);

        const t = now / 1000;
        const geom = this.geometry();
        const cam = this.camera(now, geom);
        this.upload(t);

        gl.useProgram(this.program);
        const pos = gl.getAttribLocation(this.program, "a_pos");
        gl.bindBuffer(gl.ARRAY_BUFFER, this.buffer);
        gl.enableVertexAttribArray(pos);
        gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 0, 0);

        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, this.texture);
        this.uniform1i("uLeds", 0);
        this.uniform3("uCamPos", cam.pos);
        this.uniform3("uCamR", cam.r);
        this.uniform3("uCamU", cam.u);
        this.uniform3("uCamF", cam.f);
        this.uniform1("uTanFov", Math.tan(35 * Math.PI / 360));
        this.uniform1("uAspect", this.canvas.width / Math.max(1, this.canvas.height));
        this.uniform1("uInnerR", geom.capRi);
        this.uniform1("uOuterR", geom.capRo);
        this.uniform1("uCapE", geom.capE);
        this.uniform1("uRimR", geom.R);
        this.uniform1("uFlangeOuter", geom.flangeOuter);
        this.uniform1("uFlangeHalf", geom.wall);
        this.uniform1("uWallT", geom.wall);
        this.uniform1("uBounces", this.bounces);
        this.uniform1("uAtten", 0.80);
        this.uniform1("uCutoff", 0.03);
        this.uniform1("uBandHalf", (LED_DIAMETER_MM / 2) * SCALE);
        this.uniform1("uExposure", this.exposure);
        this.uniform1("uDichroic", 1.0);
        this.uniform1("uGlass", 0.10);
        this.uniform1("uShowRim", 1.0);
        this.uniform1("uDiUpA", 0.00);
        this.uniform1("uDiUpB", 0.67);
        this.uniform1("uDiLoA", 0.17);
        this.uniform1("uDiLoB", 0.47);
        this.uniform1("uDiEdge", 0.35);
        this.uniform1("uDiBounce", 0.02);
        gl.drawArrays(gl.TRIANGLES, 0, 6);
    }

    uniform1(name, value) {
        this.gl.uniform1f(this.gl.getUniformLocation(this.program, name), value);
    }

    uniform1i(name, value) {
        this.gl.uniform1i(this.gl.getUniformLocation(this.program, name), value);
    }

    uniform3(name, v) {
        this.gl.uniform3f(this.gl.getUniformLocation(this.program, name), v[0], v[1], v[2]);
    }

    drawFallback(now) {
        drawHelioTracePreview(this.canvas, now / 1000, this.sampler, {
            ledCount: this.ledCount,
            background: "transparent",
            glow: true,
        });
    }
}

export const helioTrace = new HelioTrace({ interactive: true, bounces: 26, maxDpr: 1.45 });

const previewRenderers = new WeakMap();

export function drawHelioTraceWebGLPreview(canvas, t, sampler, opts = {}) {
    if (!canvas) return false;
    let renderer = previewRenderers.get(canvas);
    if (!renderer) {
        renderer = new HelioTrace({ interactive: false, bounces: 12, maxDpr: 1.0 });
        previewRenderers.set(canvas, renderer);
    }
    renderer.setSampler(sampler);
    renderer.setLedCount(opts.ledCount || 311);
    renderer.setExposure(opts.exposure || 2.6);
    renderer.setDetail({
        bounces: opts.bounces || 12,
        maxDpr: opts.maxDpr || 1.0,
        autoSpin: opts.autoSpin !== false,
    });
    renderer.mount(canvas);
    renderer.draw(t * 1000);
    return !!renderer.gl;
}

export function drawHelioTracePreview(canvas, t, sampler, opts = {}) {
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;
    const w = canvas.width || 1;
    const h = canvas.height || 1;
    const cx = w / 2;
    const cy = h / 2;
    const r = Math.min(w, h) * (opts.radius || 0.39);
    const samples = Math.max(48, Math.min(140, Math.round((opts.ledCount || 311) / 3)));
    const sample = typeof sampler === "function" ? sampler : fallbackSample;

    ctx.clearRect(0, 0, w, h);
    if (opts.background !== "transparent") {
        const bg = ctx.createLinearGradient(0, 0, w, h);
        bg.addColorStop(0, "#05070d");
        bg.addColorStop(0.58, "#0b101c");
        bg.addColorStop(1, "#080a12");
        ctx.fillStyle = bg;
        ctx.fillRect(0, 0, w, h);
    }

    const shell = ctx.createRadialGradient(cx - r * 0.24, cy - r * 0.32, r * 0.08, cx, cy, r * 1.15);
    shell.addColorStop(0, "rgba(255,255,255,0.22)");
    shell.addColorStop(0.42, "rgba(21,38,64,0.38)");
    shell.addColorStop(1, "rgba(3,6,12,0.96)");
    ctx.fillStyle = shell;
    ctx.beginPath();
    ctx.arc(cx, cy, r * 1.05, 0, TAU);
    ctx.fill();

    ctx.save();
    ctx.globalCompositeOperation = "lighter";
    for (let pass = 0; pass < 4; pass++) {
        const depth = 0.25 + pass * 0.18;
        const radius = r * (0.34 + pass * 0.18);
        const dot = Math.max(1.1, r * (0.018 + pass * 0.004));
        for (let i = 0; i < samples; i++) {
            const u = i / samples;
            const a = u * TAU + t * (0.18 + pass * 0.045) + pass * 0.8;
            const wobble = Math.sin(u * TAU * 5 + t * 0.9 + pass) * r * 0.04;
            const x = cx + Math.cos(a) * (radius + wobble);
            const y = cy + Math.sin(a) * radius * (0.32 + depth * 0.42);
            const c = sample((u + pass * 0.11) % 1, depth, t) || [0, 0, 0];
            const alpha = 0.12 + pass * 0.08;
            ctx.fillStyle = `rgba(${clampByte(c[0])},${clampByte(c[1])},${clampByte(c[2])},${alpha})`;
            ctx.beginPath();
            ctx.arc(x, y, dot, 0, TAU);
            ctx.fill();
        }
    }
    ctx.restore();

    const shine = ctx.createLinearGradient(cx - r, cy - r, cx + r, cy + r);
    shine.addColorStop(0, "rgba(255,255,255,0.34)");
    shine.addColorStop(0.2, "rgba(255,255,255,0.02)");
    shine.addColorStop(0.72, "rgba(255,188,92,0.08)");
    shine.addColorStop(1, "rgba(255,255,255,0.18)");
    ctx.strokeStyle = shine;
    ctx.lineWidth = Math.max(1, r * 0.035);
    ctx.beginPath();
    ctx.arc(cx, cy, r * 1.02, 0, TAU);
    ctx.stroke();

    ctx.fillStyle = "rgba(255,255,255,0.34)";
    ctx.beginPath();
    ctx.ellipse(cx - r * 0.28, cy - r * 0.38, r * 0.22, r * 0.08, -0.55, 0, TAU);
    ctx.fill();
}
