// HelioTrace: HELIO's mirrored single-strip ball renderer.
//
// This is the firmware-friendly version of the helio.lighting raymarcher. It
// keeps the actual optical model from the live renderer: an analytical acrylic
// shell, a single equator LED texture with package/gap subpixels, warm-white
// dichroic handling, recursive internal bounces, and the narrow equatorial rim.
// It intentionally avoids Three.js so the controller UI still works when the
// browser is connected directly to the HELIO access point with no internet.

const TAU = Math.PI * 2;
const LEDS_PER_METER = 144;
const LED_DIAMETER_MM = 5.0;
const LED_PITCH_MM = 1000 / LEDS_PER_METER;
const DEFAULT_LED_FILL = 1.0;
const DEFAULT_LED_GAIN = 8.0;
const DEFAULT_REFLECTION_SPREAD = 0.0;
const DEFAULT_GLOW = 1.65;
const DEFAULT_WALL_GLOW = 1.25;
const DEFAULT_WALL_DISTANCE_CM = 25;
const DEFAULT_OFF_THRESHOLD = 12;
const DEFAULT_OFF_BRIGHTNESS = 0.30;
const DEFAULT_ATTENUATION = 0.80;
const DEFAULT_CUTOFF = 0.03;
const DEFAULT_WALL_MM = 3.0;
const DEFAULT_TRACE_FPS = 30;
const DEFAULT_LED_COUNT = 222;
const ROOM_SIZE_MM = 3000;
const LED_EMISSION_DEGREES = 110;
const TILT_PERIOD_MS = 36000;
const SWIVEL_PERIOD_MS = 48000;
const UNLIT_LED_RGB = [3, 3, 3];
const LED_TEXTURE_SUBPIXELS = 24;
const SCALE = 0.01;
const WW = [1.0, 0.69, 0.43];
const PREVIEW_ARCS = [24, 40, 58, 71, 90, 106, 128, 145];

function clampByte(v) {
    return Math.max(0, Math.min(255, Math.round(Number(v) || 0)));
}

function clamp(v, lo, hi) {
    return Math.max(lo, Math.min(hi, v));
}

function finiteNumber(value, fallback) {
    const number = Number(value);
    return Number.isFinite(number) ? number : fallback;
}

// Reflections use the same lit package colors as the physical strip. Package
// shape and gaps are applied when the texture is built; inactive packages stay
// visible only as direct geometry.
export function buildHelioReflectionColors(colors, brightness,
                                           threshold = DEFAULT_OFF_THRESHOLD,
                                           output = null) {
    const count = brightness?.length || 0;
    const result = output && output.length === count * 3 ? output : new Uint8Array(count * 3);
    result.fill(0);
    if (!colors || count === 0) return result;

    const floor = clamp(finiteNumber(threshold, DEFAULT_OFF_THRESHOLD), 0, 255);
    for (let i = 0; i < count; i++) {
        if ((brightness[i] || 0) <= floor) continue;
        const target = i * 3;
        result[target] = clampByte(colors[target]);
        result[target + 1] = clampByte(colors[target + 1]);
        result[target + 2] = clampByte(colors[target + 2]);
    }
    return result;
}

// A circular Gaussian convolution maps the 110-degree LED emission cone to a
// diffuse wall signal while preserving the position of moving light sources.
export function buildHelioWallColors(colors, brightness,
                                     threshold = DEFAULT_OFF_THRESHOLD,
                                     output = null) {
    const count = brightness?.length || 0;
    const result = output && output.length === count * 3 ? output : new Uint8Array(count * 3);
    result.fill(0);
    if (!colors || count === 0) return result;

    const floor = clamp(finiteNumber(threshold, DEFAULT_OFF_THRESHOLD), 0, 255);
    const sigma = Math.max(0.5, count * LED_EMISSION_DEGREES / 360 / 6);
    const radius = Math.min(Math.floor(count / 2), Math.ceil(sigma * 3));
    let kernelWeight = 0;
    for (let offset = -radius; offset <= radius; offset++) {
        kernelWeight += Math.exp(-0.5 * (offset / sigma) ** 2);
    }

    for (let target = 0; target < count; target++) {
        let red = 0;
        let green = 0;
        let blue = 0;
        for (let offset = -radius; offset <= radius; offset++) {
            const source = (target + offset + count) % count;
            if ((brightness[source] || 0) <= floor) continue;
            const weight = Math.exp(-0.5 * (offset / sigma) ** 2);
            const sourceOffset = source * 3;
            red += clampByte(colors[sourceOffset]) * weight;
            green += clampByte(colors[sourceOffset + 1]) * weight;
            blue += clampByte(colors[sourceOffset + 2]) * weight;
        }
        const targetOffset = target * 3;
        result[targetOffset] = clampByte(red / kernelWeight);
        result[targetOffset + 1] = clampByte(green / kernelWeight);
        result[targetOffset + 2] = clampByte(blue / kernelWeight);
    }
    return result;
}

export function helioTiltPitch(phase, maxPitch = Math.PI / 2) {
    const wave = Math.sin(finiteNumber(phase, 0));
    return finiteNumber(maxPitch, Math.PI / 2) * wave * wave * wave;
}

export function helioSwivelPosition(phase, distance, angle = Math.PI / 4) {
    const radius = Math.max(0, finiteNumber(distance, 0));
    const offset = clamp(finiteNumber(angle, Math.PI / 4), 0, Math.PI / 2);
    const lateral = radius * Math.sin(offset);
    const depth = radius * Math.cos(offset);
    const orbit = finiteNumber(phase, 0);
    return [Math.cos(orbit) * lateral, Math.sin(orbit) * lateral, depth];
}

function tiltPhaseForPitch(pitch) {
    const normalized = clamp(finiteNumber(pitch, 0) / (Math.PI / 2), -1, 1);
    return Math.asin(Math.cbrt(normalized));
}

function renderDpr(maxDpr = 1) {
    const dpr = Number(maxDpr);
    return clamp(Number.isFinite(dpr) && dpr > 0 ? dpr : 1, 0.35, 12.0);
}

function resizeCanvas(canvas, maxDpr = 1.5) {
    const dpr = renderDpr(maxDpr);
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

function createLinearTexture(gl) {
    const texture = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, texture);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    // WebGL 1 requires clamp wrapping for non-power-of-two texture widths. The
    // shader wraps azimuths before sampling either texture.
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
    return texture;
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

function cameraBasis(pos, target, poleRight = [1, 0, 0]) {
    const f = normalize([target[0] - pos[0], target[1] - pos[1], target[2] - pos[2]]);
    const lateral = cross(f, [0, 1, 0]);
    const r = Math.hypot(lateral[0], lateral[1], lateral[2]) > 1e-8
        ? normalize(lateral)
        : normalize(poleRight);
    const u = normalize(cross(r, f));
    return { r, u, f };
}

function inferDiameterMm(ledCount) {
    const count = Math.max(1, Number(ledCount) || DEFAULT_LED_COUNT);
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
uniform float uBounces, uAtten, uCutoff, uBandHalf, uExposure, uDichroic, uGlass, uShowRim, uLedGain, uLedBlur, uLedUvPitch, uLedPitch, uPixelWorld;
uniform float uGlow, uWallGlow, uHasWall, uWallPlaneZ, uWallDistance, uRoomHalf;
uniform float uDiUpA, uDiUpB, uDiLoA, uDiLoB, uDiEdge, uDiBounce;
uniform sampler2D uLeds, uReflections, uWallLeds;
#define PI 3.14159265359

vec4 ledTexel(float u){
    return texture2D(uLeds, vec2(fract(u), 0.5));
}

vec3 reflectedLedTex(float u){
    float footprint = max(uLedUvPitch * 0.08, 1e-6);
    vec3 center = texture2D(uReflections, vec2(fract(u), 0.5)).rgb * 0.50;
    vec3 sides = texture2D(uReflections, vec2(fract(u - footprint), 0.5)).rgb;
    sides += texture2D(uReflections, vec2(fract(u + footprint), 0.5)).rgb;
    return center + sides * 0.25;
}

vec3 ledColorAt(float az, float blur){
    float u = fract(az / (2.0 * PI) + 1.0);
    blur = max(0.0, blur);
    vec3 c = reflectedLedTex(u) * 0.34;
    c += (reflectedLedTex(u - blur) + reflectedLedTex(u + blur)) * 0.19;
    c += (reflectedLedTex(u - blur * 2.2) + reflectedLedTex(u + blur * 2.2)) * 0.10;
    c += (reflectedLedTex(u - blur * 4.0) + reflectedLedTex(u + blur * 4.0)) * 0.04;
    return c;
}

vec3 wallLedTex(float u){
    return texture2D(uWallLeds, vec2(fract(u), 0.5)).rgb;
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
    const vec3 WW = vec3(1.0, 0.69, 0.43);
    float W = min(min(c.r / WW.r, c.g / WW.g), c.b / WW.b);
    vec3 chromatic = max(c - W * WW, vec3(0.0));
    vec3 hue = hue2rgb(phase);
    float hl = dot(hue, vec3(0.299, 0.587, 0.114));
    hue *= clamp(0.6 / max(hl, 0.05), 0.7, 3.5);
    float ds = clamp(dStrength, 0.0, 1.0);
    vec3 chrom = chromatic * (1.0 - 0.7 * ds);
    vec3 white = W * mix(WW, hue, ds);
    return (chrom + white) * uLedGain;
}

vec3 sampledEmission(float u, float phase, float dStrength){
    vec4 sample = ledTexel(u);
    vec3 emitted = ledEmit(sample.rgb, phase, dStrength);
    return emitted;
}

vec3 centerEmitterColor(float projectionU){
    vec3 emitted = ledEmit(wallLedTex(projectionU), 0.03, uDichroic * 0.10) * 0.012;
    float throughput = uAtten;
    int projectionCount = int(min(uBounces, 8.0));
    for(int k = 0; k < 8; k++){
        if(k >= projectionCount) break;
        float bounce = float(k + 1);
        float mirroredU = mod(bounce, 2.0) < 0.5 ? projectionU : 1.0 - projectionU;
        vec3 reflected = ledEmit(wallLedTex(mirroredU + bounce * 0.008),
            0.03 + bounce * uDiBounce, uDichroic * min(1.0, 0.18 + bounce * 0.08));
        emitted += reflected * throughput * 0.005;
        throughput *= uAtten;
    }
    return emitted;
}

vec3 centerEmitterRadiance(vec3 ro, vec3 rd){
    if(uHasWall < 0.5) return vec3(0.0);
    float closestT = -dot(ro, rd);
    if(closestT <= 0.0) return vec3(0.0);
    vec3 closest = ro + rd * closestT;
    float sourceDistance = length(closest);
    float coreRadius = max(uBandHalf * 2.0, uInnerR * 0.025);
    float haloRadius = max(coreRadius * 2.5, uInnerR * 0.10);
    if(sourceDistance > haloRadius * 4.0) return vec3(0.0);
    float core = exp(-0.5 * sourceDistance * sourceDistance /
        max(coreRadius * coreRadius, 1e-5));
    float halo = exp(-0.5 * sourceDistance * sourceDistance /
        max(haloRadius * haloRadius, 1e-5));
    vec3 viewDirection = normalize(ro);
    float emissionAzimuth = atan(-viewDirection.z, -viewDirection.x);
    float projectionU = fract(emissionAzimuth / (2.0 * PI) + 1.0);
    float elevationAngle = asin(clamp(viewDirection.y, -1.0, 1.0));
    float emissionSigma = 0.8154;
    float cone = exp(-0.5 * elevationAngle * elevationAngle /
        (emissionSigma * emissionSigma));
    return centerEmitterColor(projectionU) * cone * (core * 0.18 + halo * 0.018);
}

bool roomIntersection(vec3 ro, vec3 rd, out vec3 point, out float surface){
    float best = 1e9;
    float frontZ = uWallPlaneZ + uRoomHalf * 2.0;
    surface = -1.0;

    if(abs(rd.z) > 1e-5){
        float backT = (uWallPlaneZ - ro.z) / rd.z;
        vec3 backPoint = ro + rd * backT;
        if(backT > 0.0 && abs(backPoint.x) <= uRoomHalf &&
           abs(backPoint.y) <= uRoomHalf && backT < best){
            best = backT; point = backPoint; surface = 0.0;
        }
        float frontT = (frontZ - ro.z) / rd.z;
        vec3 frontPoint = ro + rd * frontT;
        if(frontT > 0.0 && abs(frontPoint.x) <= uRoomHalf &&
           abs(frontPoint.y) <= uRoomHalf && frontT < best){
            best = frontT; point = frontPoint; surface = 1.0;
        }
    }
    if(abs(rd.x) > 1e-5){
        float sideX = rd.x > 0.0 ? uRoomHalf : -uRoomHalf;
        float sideT = (sideX - ro.x) / rd.x;
        vec3 sidePoint = ro + rd * sideT;
        if(sideT > 0.0 && abs(sidePoint.y) <= uRoomHalf &&
           sidePoint.z >= uWallPlaneZ && sidePoint.z <= frontZ && sideT < best){
            best = sideT; point = sidePoint; surface = 2.0;
        }
    }
    if(abs(rd.y) > 1e-5){
        float horizontalY = rd.y > 0.0 ? uRoomHalf : -uRoomHalf;
        float horizontalT = (horizontalY - ro.y) / rd.y;
        vec3 horizontalPoint = ro + rd * horizontalT;
        if(horizontalT > 0.0 && abs(horizontalPoint.x) <= uRoomHalf &&
           horizontalPoint.z >= uWallPlaneZ && horizontalPoint.z <= frontZ &&
           horizontalT < best){
            best = horizontalT; point = horizontalPoint; surface = 3.0;
        }
    }
    return surface >= 0.0;
}

vec3 environmentColor(vec3 ro, vec3 rd){
    vec3 openGlow = vec3(0.0007, 0.0010, 0.0018);
    if(uHasWall < 0.5) return openGlow;

    vec3 roomPoint;
    float roomSurface;
    if(!roomIntersection(ro, rd, roomPoint, roomSurface)) return openGlow;
    vec2 roomUv = roomPoint.xy / max(uRoomHalf, 1e-5);
    if(roomSurface > 1.5 && roomSurface < 2.5){
        roomUv = vec2((roomPoint.z - uWallPlaneZ) / uRoomHalf - 1.0,
                      roomPoint.y / uRoomHalf);
    } else if(roomSurface > 2.5){
        roomUv = vec2(roomPoint.x / uRoomHalf,
                      (roomPoint.z - uWallPlaneZ) / uRoomHalf - 1.0);
    }
    float roomShade = mix(0.78, 1.03, clamp(roomUv.y * 0.5 + 0.5, 0.0, 1.0));
    roomShade *= 1.0 - 0.12 * dot(roomUv, roomUv);
    vec3 wallBase = vec3(0.0042, 0.0033, 0.0029) * roomShade *
        (roomSurface < 0.5 ? 1.0 : 0.72);
    if(roomSurface > 0.5) return wallBase;

    vec3 sourceRay = roomPoint;
    float sourceDistance = length(sourceRay);
    float axialDistance = max(-sourceRay.z, 1e-5);
    vec2 wallPoint = sourceRay.xy;
    // The strip lies in the sphere's x/z equator. An inward-facing LED emits
    // through the center along the direction opposite its position on that ring.
    float emissionAzimuth = atan(-sourceRay.z, -sourceRay.x);
    float wallU = fract(emissionAzimuth / (2.0 * PI) + 1.0);
    float elevationAngle = asin(clamp(sourceRay.y / max(sourceDistance, 1e-5), -1.0, 1.0));
    float emissionSigma = 0.8154;
    float cone = exp(-0.5 * elevationAngle * elevationAngle /
        (emissionSigma * emissionSigma));
    float wallIncidence = axialDistance / max(sourceDistance, 1e-5);
    float centerToWall = max(-uWallPlaneZ, 1e-5);
    float distanceFalloff = centerToWall * centerToWall /
        max(sourceDistance * sourceDistance, 1e-5);
    vec3 projected = centerEmitterColor(wallU) * cone * wallIncidence * distanceFalloff;

    if(uShowRim > 0.5){
        float shadowWidth = max(uWallT * 4.0,
            uFlangeHalf * centerToWall / max(uInnerR, 1e-5));
        float rimShadow = mix(0.18, 1.0,
            smoothstep(shadowWidth * 0.72, shadowWidth * 1.35, abs(wallPoint.y)));
        projected *= rimShadow;
    }

    return wallBase + projected * uWallGlow;
}

float radialPointWeight(float tangentDistance, float verticalDistance, float haloSigma){
    float radialDistance = length(vec2(tangentDistance, verticalDistance)) / max(uBandHalf, 1e-5);
    float core = max(0.0, 1.0 - radialDistance * radialDistance);
    float halo = exp(-0.5 * radialDistance * radialDistance / (haloSigma * haloSigma)) * 0.15 * uGlow;
    return core + halo;
}

vec3 bandContribution(vec3 P, vec3 dir, vec3 nrm, float phase, float dStr, float blur, float throughput, float direct){
    float face = max(0.0, dot(dir, nrm));
    float uvPitch = max(uLedUvPitch, 1e-6);
    float haloSigma = 1.0 + min(blur / uvPitch, 6.0) * 0.50;
    if(face <= 0.0 || abs(P.y) > uBandHalf * haloSigma * 4.5) return vec3(0.0);

    float u = fract(atan(P.z, P.x) / (2.0 * PI) + 1.0);
    if(direct < 0.5){
        float reflectedHalf = max(uBandHalf * haloSigma, uPixelWorld * 0.72);
        float verticalDistance = P.y / max(reflectedHalf, 1e-5);
        float core = exp(-0.5 * verticalDistance * verticalDistance);
        float halo = exp(-0.08 * verticalDistance * verticalDistance) * 0.12 * uGlow;
        vec3 emitted = ledEmit(reflectedLedTex(u), phase, dStr);
        return throughput * sqrt(face) * emitted * (core + halo);
    }

    float ledOffset = fract(u / uvPitch) - 0.5;
    float centerU = fract(u - ledOffset * uvPitch);
    float tangentDistance = ledOffset * uLedPitch;
    float centerWeight = radialPointWeight(tangentDistance, P.y, haloSigma);
    float previousWeight = radialPointWeight(tangentDistance + uLedPitch, P.y, haloSigma);
    float nextWeight = radialPointWeight(tangentDistance - uLedPitch, P.y, haloSigma);
    vec3 emitted = sampledEmission(centerU, phase, dStr) * centerWeight;
    emitted += sampledEmission(centerU - uvPitch, phase, dStr) * previousWeight;
    emitted += sampledEmission(centerU + uvPitch, phase, dStr) * nextWeight;
    return throughput * face * emitted;
}

vec3 traceTunnel(vec3 P, vec3 dir, float exitPhase, float dStr){
    float tp = 1.0;
    vec3 col = vec3(0.0);
    int maxB = int(uBounces);
    {
        float cy0 = (P.y >= 0.0) ? -uCapE : uCapE;
        vec3 n0 = normalize(P - vec3(0.0, cy0, 0.0));
        col += bandContribution(P, dir, n0, exitPhase, dStr, uLedBlur * 0.18, tp, 1.0);
    }
    for(int k = 0; k < 64; k++){
        if(k >= maxB) break;
        float t; vec3 nrm;
        if(!nextCapHit(P, dir, uInnerR, t, nrm)) break;
        vec3 Pn = P + dir * t;
        float phase = exitPhase + float(k + 1) * uDiBounce;
        float reflectionBlur = uLedBlur * (0.95 + float(k) * 0.16);
        col += bandContribution(Pn, dir, nrm, phase, dStr, reflectionBlur, tp, 0.0);
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

    vec3 envCol = environmentColor(ro, rd);
    vec3 outCol = envCol;
    float sphereT = 1e9;

    float oT; vec3 nO;
    bool hitOuter = nextCapHit(ro, rd, uOuterR, oT, nO) && oT > 0.0;
    if(hitOuter){
        sphereT = oT;
        vec3 pO = ro + rd * oT;
        float fres = pow(1.0 - abs(dot(nO, rd)), 3.0);
        vec3 glass = vec3(0.025, 0.07, 0.055) * fres * uGlass;

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
            float transmission = 1.0 - fres * 0.025;
            outCol = envCol * transmission + traceTunnel(P, rdIn, exitPhase, dStr) + glass;
        } else {
            outCol = envCol + glass;
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
                float az = atan(mid.z, mid.x);
                float brush = 0.85 + 0.15 * sin(az * 240.0);
                vec3 fn = vec3(0.0, rd.y > 0.0 ? -1.0 : 1.0, 0.0);
                float ff = pow(1.0 - abs(dot(fn, rd)), 4.0);
                vec3 rimLed = ledEmit(ledColorAt(az, uLedBlur * 0.50), 0.08, uDichroic * 0.18);
                vec3 metal = envCol * 0.48 + vec3(0.010, 0.007, 0.005) * brush;
                metal += rimLed * (0.19 + ff * 0.24) + vec3(0.022, 0.016, 0.011) * ff;
                if(tIn < sphereT) outCol = metal;
            }
        }
    }
    if(hitOuter) outCol += centerEmitterRadiance(ro, rd);

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
        this.reflectionTexture = null;
        this.wallTexture = null;
        this.buffer = null;
        this.sampler = fallbackSample;
        this.ledCount = DEFAULT_LED_COUNT;
        this.diameterMm = inferDiameterMm(this.ledCount);
        this.wallMm = DEFAULT_WALL_MM;
        this.cutoffDeg = 180;
        this.exposure = 3.1;
        this.bounces = opts.bounces || 28;
        this.maxDpr = opts.maxDpr || 1.45;
        this.dichroic = opts.dichroic ?? 0.55;
        this.glass = opts.glass ?? 0.10;
        this.showRim = opts.showRim !== false;
        this.ledFill = opts.ledFill ?? DEFAULT_LED_FILL;
        this.ledGain = opts.ledGain ?? DEFAULT_LED_GAIN;
        this.reflectionSpread = opts.reflectionSpread ?? DEFAULT_REFLECTION_SPREAD;
        this.glow = opts.glow ?? DEFAULT_GLOW;
        this.wallGlow = opts.wallGlow ?? DEFAULT_WALL_GLOW;
        this.wallDistanceCm = opts.wallDistanceCm ?? DEFAULT_WALL_DISTANCE_CM;
        this.offThreshold = opts.offThreshold ?? DEFAULT_OFF_THRESHOLD;
        this.offBrightness = opts.offBrightness ?? DEFAULT_OFF_BRIGHTNESS;
        this.attenuation = opts.attenuation ?? DEFAULT_ATTENUATION;
        this.cutoff = opts.cutoff ?? DEFAULT_CUTOFF;
        this.wall = opts.wall !== false;
        this.interactive = !!opts.interactive;
        this.autoSpin = opts.autoSpin === true;
        this.preserveDrawingBuffer = !!opts.preserveDrawingBuffer;
        this.targetFps = clamp(Number(opts.fps) || DEFAULT_TRACE_FPS, 1, 60);
        this.running = false;
        this.raf = 0;
        this.lastFrameAt = 0;
        this.subpixels = 18;
        this.data = new Uint8Array(1);
        this.reflectionData = new Uint8Array(1);
        this.wallData = new Uint8Array(1);
        this.wallColors = new Uint8Array(3);
        this.sourceRgb = new Uint8Array(3);
        this.sourceBrightness = new Uint8Array(1);
        this.previewMax = 0;
        this.previewLit = 0;
        this.yaw = 0.868;
        this.pitch = 0.39;
        this.distance = 3.72;
        const now = performance.now();
        this.swivelAnimation = opts.swivelAnimation === true;
        this.tiltAnimation = opts.tiltAnimation === true && !this.swivelAnimation;
        this.tiltEpoch = now -
            tiltPhaseForPitch(this.pitch) * TILT_PERIOD_MS / TAU;
        this.swivelEpoch = now -
            this.swivelPhaseForAngles(this.yaw, this.pitch) * SWIVEL_PERIOD_MS / TAU;
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
            const now = performance.now();
            this.draw(now);
            this.lastFrameAt = now;
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
            preserveDrawingBuffer: this.preserveDrawingBuffer,
        });
        if (!gl) throw new Error("webgl context unavailable");
        this.gl = gl;
        gl.disable(gl.DITHER);
        this.program = program(gl, VERT, FRAG);
        this.buffer = gl.createBuffer();
        gl.bindBuffer(gl.ARRAY_BUFFER, this.buffer);
        gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([
            -1, -1,  1, -1, -1,  1,
            -1,  1,  1, -1,  1,  1,
        ]), gl.STATIC_DRAW);

        this.texture = createLinearTexture(gl);
        this.reflectionTexture = createLinearTexture(gl);
        this.wallTexture = createLinearTexture(gl);
        this.ensureTextureStorage();
    }

    bindPointerControls() {
        if (this.canvas._helioTraceControls) return;
        this.canvas._helioTraceControls = true;
        this.canvas.addEventListener("pointerdown", (ev) => {
            const now = performance.now();
            const angles = this.currentCameraAngles(now);
            this.yaw = angles.yaw;
            this.pitch = angles.pitch;
            this.drag = {
                x: ev.clientX,
                y: ev.clientY,
                yaw: this.yaw,
                pitch: this.pitch,
                mode: this.cameraAnimationMode(),
            };
            this.canvas.setPointerCapture(ev.pointerId);
        });
        this.canvas.addEventListener("pointermove", (ev) => {
            if (!this.drag) return;
            this.yaw = this.drag.yaw + (ev.clientX - this.drag.x) * 0.008;
            this.pitch = clamp(this.drag.pitch + (ev.clientY - this.drag.y) * 0.006,
                -Math.PI / 2, Math.PI / 2);
            this.draw(performance.now());
        });
        const end = () => {
            const mode = this.drag?.mode;
            this.drag = null;
            if (mode === "tilt" && this.tiltAnimation) {
                this.tiltEpoch = performance.now() -
                    tiltPhaseForPitch(this.pitch) * TILT_PERIOD_MS / TAU;
            } else if (mode === "swivel" && this.swivelAnimation) {
                this.swivelEpoch = performance.now() -
                    this.swivelPhaseForAngles(this.yaw, this.pitch) * SWIVEL_PERIOD_MS / TAU;
            }
        };
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
        const count = Math.max(1, Math.min(1200, Math.round(Number(n) || DEFAULT_LED_COUNT)));
        if (count === this.ledCount) return;
        this.ledCount = count;
        this.diameterMm = inferDiameterMm(count);
        this.ensureTextureStorage();
    }

    setExposure(v) {
        this.exposure = Math.max(0.5, Math.min(7.0, Number(v) || 2.5));
    }

    setDetail(opts = {}) {
        if (opts.bounces !== undefined) {
            const bounces = Number(opts.bounces);
            this.bounces = clamp(Number.isFinite(bounces) ? bounces : this.bounces, 0, 25);
        }
        if (opts.maxDpr !== undefined) this.maxDpr = clamp(Number(opts.maxDpr) || this.maxDpr, 0.35, 12.0);
        if (opts.dichroic !== undefined) this.dichroic = clamp(Number(opts.dichroic) || 0, 0.0, 1.0);
        if (opts.glass !== undefined) this.glass = clamp(Number(opts.glass) || 0, 0.0, 0.12);
        if (opts.showRim !== undefined) this.showRim = !!opts.showRim;
        if (opts.ledFill !== undefined) this.ledFill = clamp(Number(opts.ledFill) || DEFAULT_LED_FILL, 0.25, 1.35);
        if (opts.ledGain !== undefined) this.ledGain = clamp(Number(opts.ledGain) || DEFAULT_LED_GAIN, 0.5, 8.0);
        if (opts.reflectionSpread !== undefined) this.reflectionSpread = clamp(Number(opts.reflectionSpread) || DEFAULT_REFLECTION_SPREAD, 0.0, 4.0);
        if (opts.glow !== undefined) this.glow = clamp(Number(opts.glow) || 0, 0.0, 3.0);
        if (opts.wallGlow !== undefined) this.wallGlow = clamp(Number(opts.wallGlow) || 0, 0.0, 3.0);
        if (opts.wallDistanceCm !== undefined) {
            this.wallDistanceCm = clamp(finiteNumber(opts.wallDistanceCm, this.wallDistanceCm), 0, 200);
        }
        if (opts.offThreshold !== undefined) this.offThreshold = clamp(finiteNumber(opts.offThreshold, this.offThreshold), 0, 64);
        if (opts.offBrightness !== undefined) this.offBrightness = clamp(finiteNumber(opts.offBrightness, this.offBrightness), 0, 1);
        if (opts.attenuation !== undefined) this.attenuation = clamp(finiteNumber(opts.attenuation, this.attenuation), 0.4, 0.98);
        if (opts.cutoff !== undefined) this.cutoff = clamp(finiteNumber(opts.cutoff, this.cutoff), 0.001, 0.25);
        if (opts.fps !== undefined) this.targetFps = clamp(finiteNumber(opts.fps, this.targetFps), 1, 60);
        if (opts.wall !== undefined) this.wall = !!opts.wall;
        if (opts.autoSpin !== undefined) this.autoSpin = !!opts.autoSpin;
        if (opts.tiltAnimation !== undefined || opts.swivelAnimation !== undefined) {
            const mode = opts.swivelAnimation === true
                ? "swivel"
                : opts.tiltAnimation === true ? "tilt" : "static";
            this.setCameraAnimation(mode);
        }
    }

    setTiltAnimation(enabled) {
        this.setCameraAnimation(enabled ? "tilt" : "static");
    }

    setSwivelAnimation(enabled) {
        this.setCameraAnimation(enabled ? "swivel" : "static");
    }

    cameraAnimationMode() {
        if (this.swivelAnimation) return "swivel";
        if (this.tiltAnimation) return "tilt";
        return "static";
    }

    cameraAnimationActive() {
        return this.tiltAnimation || this.swivelAnimation;
    }

    setCameraAnimation(mode) {
        const next = mode === "swivel" ? "swivel" : mode === "tilt" ? "tilt" : "static";
        if (next === this.cameraAnimationMode()) return;
        const now = performance.now();
        const angles = this.currentCameraAngles(now);
        this.yaw = angles.yaw;
        this.pitch = angles.pitch;
        this.tiltAnimation = next === "tilt";
        this.swivelAnimation = next === "swivel";
        if (this.tiltAnimation) {
            this.tiltEpoch = now - tiltPhaseForPitch(this.pitch) * TILT_PERIOD_MS / TAU;
        } else if (this.swivelAnimation) {
            this.swivelEpoch = now -
                this.swivelPhaseForAngles(this.yaw, this.pitch) * SWIVEL_PERIOD_MS / TAU;
        }
    }

    swivelPhaseForAngles(yaw, pitch) {
        const cp = Math.cos(pitch);
        return Math.atan2(Math.sin(pitch), Math.cos(yaw) * cp);
    }

    currentSwivelPhase(now) {
        return (now - this.swivelEpoch) * TAU / SWIVEL_PERIOD_MS;
    }

    currentCameraAngles(now) {
        if (this.swivelAnimation && !this.drag) {
            const pos = helioSwivelPosition(this.currentSwivelPhase(now), 1);
            return {
                yaw: Math.atan2(pos[2], pos[0]),
                pitch: Math.asin(clamp(pos[1], -1, 1)),
            };
        }
        return { yaw: this.yaw, pitch: this.currentPitch(now) };
    }

    ensureTextureStorage() {
        if (!this.gl) return;
        const maxTex = this.gl.getParameter(this.gl.MAX_TEXTURE_SIZE) || 4096;
        const wanted = LED_TEXTURE_SUBPIXELS;
        this.subpixels = Math.max(3, Math.min(wanted, Math.floor(maxTex / Math.max(1, this.ledCount))));
        this.data = new Uint8Array(this.ledCount * this.subpixels * 4);
        this.reflectionData = new Uint8Array(this.ledCount * this.subpixels * 4);
        this.wallData = new Uint8Array(this.ledCount * 4);
        this.wallColors = new Uint8Array(this.ledCount * 3);
        this.sourceRgb = new Uint8Array(this.ledCount * 3);
        this.sourceBrightness = new Uint8Array(this.ledCount);
    }

    geometry() {
        const R = (this.diameterMm / 2) * SCALE;
        const wall = this.wallMm * SCALE;
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
            flangeOuter: R + 18 * SCALE,
            wallDistance: this.wallDistanceCm * 10 * SCALE,
            roomHalf: ROOM_SIZE_MM * 0.5 * SCALE,
        };
    }

    currentPitch(now) {
        if (!this.tiltAnimation || this.drag) return this.pitch;
        const phase = (now - this.tiltEpoch) * TAU / TILT_PERIOD_MS;
        return helioTiltPitch(phase);
    }

    camera(now, geom) {
        const d = geom.R * this.distance;
        if (this.swivelAnimation && !this.drag) {
            const pos = helioSwivelPosition(this.currentSwivelPhase(now), d);
            return { pos, ...cameraBasis(pos, [0, 0, 0]) };
        }
        const spin = this.autoSpin && !this.drag && !this.reducedMotion ? now * 0.000035 : 0;
        const yaw = this.yaw + spin;
        const pitch = this.currentPitch(now);
        const cp = Math.cos(pitch);
        const pos = [
            Math.cos(yaw) * cp * d,
            Math.sin(pitch) * d,
            Math.sin(yaw) * cp * d,
        ];
        const poleRight = [Math.sin(yaw), 0, -Math.cos(yaw)];
        return { pos, ...cameraBasis(pos, [0, 0, 0], poleRight) };
    }

    upload(t) {
        const gl = this.gl;
        const sub = this.subpixels;
        const halfPkg = clamp((LED_DIAMETER_MM / LED_PITCH_MM) * 0.5 * this.ledFill, 0.05, 0.49);
        let previewMax = 0;
        let previewLit = 0;
        for (let i = 0; i < this.ledCount; i++) {
            const source = this.sampler(i / this.ledCount, 0.5, t) || [0, 0, 0];
            const sourceMax = Math.max(
                clampByte(source[0]),
                clampByte(source[1]),
                clampByte(source[2]),
            );
            const lit = sourceMax > this.offThreshold;
            const c = lit
                ? source
                : UNLIT_LED_RGB.map(channel => channel * this.offBrightness);
            const r8 = clampByte(c[0]);
            const g8 = clampByte(c[1]);
            const b8 = clampByte(c[2]);
            const v = lit ? Math.max(r8, g8, b8) : 0;
            previewMax = Math.max(previewMax, v);
            if (v > 24) previewLit++;
            const sourceOffset = i * 3;
            this.sourceRgb[sourceOffset] = r8;
            this.sourceRgb[sourceOffset + 1] = g8;
            this.sourceRgb[sourceOffset + 2] = b8;
            this.sourceBrightness[i] = v;
        }

        buildHelioWallColors(this.sourceRgb, this.sourceBrightness,
            this.offThreshold, this.wallColors);

        for (let i = 0; i < this.ledCount; i++) {
            const sourceOffset = i * 3;
            const r = this.sourceRgb[sourceOffset] / 255;
            const g = this.sourceRgb[sourceOffset + 1] / 255;
            const b = this.sourceRgb[sourceOffset + 2] / 255;
            const lit = this.sourceBrightness[i] > this.offThreshold;
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
                this.reflectionData[o] = lit ? this.data[o] : 0;
                this.reflectionData[o + 1] = lit ? this.data[o + 1] : 0;
                this.reflectionData[o + 2] = lit ? this.data[o + 2] : 0;
                this.reflectionData[o + 3] = 255;
            }
            const wallSource = i * 3;
            const wallTarget = i * 4;
            this.wallData[wallTarget] = this.wallColors[wallSource];
            this.wallData[wallTarget + 1] = this.wallColors[wallSource + 1];
            this.wallData[wallTarget + 2] = this.wallColors[wallSource + 2];
            this.wallData[wallTarget + 3] = 255;
        }
        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, this.texture);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, this.ledCount * sub, 1, 0,
                      gl.RGBA, gl.UNSIGNED_BYTE, this.data);
        gl.activeTexture(gl.TEXTURE1);
        gl.bindTexture(gl.TEXTURE_2D, this.reflectionTexture);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, this.ledCount * sub, 1, 0,
                      gl.RGBA, gl.UNSIGNED_BYTE, this.reflectionData);
        gl.activeTexture(gl.TEXTURE2);
        gl.bindTexture(gl.TEXTURE_2D, this.wallTexture);
        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, this.ledCount, 1, 0,
                      gl.RGBA, gl.UNSIGNED_BYTE, this.wallData);
        this.previewMax = previewMax;
        this.previewLit = previewLit;
    }

    start() {
        if (this.running) return;
        this.running = true;
        const loop = (now) => {
            if (!this.running) return;
            const frameInterval = 1000 / (this.cameraAnimationActive() ? 60 : this.targetFps);
            if (now - this.lastFrameAt >= frameInterval - 0.5) {
                this.lastFrameAt = now;
                this.draw(now);
            }
            this.raf = requestAnimationFrame(loop);
        };
        this.raf = requestAnimationFrame(loop);
    }

    stop() {
        this.running = false;
        if (this.raf) cancelAnimationFrame(this.raf);
        this.raf = 0;
    }

    draw(now, sampleTime = now / 1000) {
        if (!this.canvas) return;
        if (!this.gl) {
            this.drawFallback(now, sampleTime);
            return;
        }
        const gl = this.gl;
        resizeCanvas(this.canvas, this.maxDpr);
        gl.viewport(0, 0, this.canvas.width, this.canvas.height);
        gl.clearColor(0, 0, 0, 1);
        gl.clear(gl.COLOR_BUFFER_BIT);

        const geom = this.geometry();
        const cam = this.camera(now, geom);
        this.upload(sampleTime);
        this.canvas.dataset.previewRenderer = "heliotrace-webgl";
        this.canvas.dataset.previewCamera = this.cameraAnimationMode();
        this.canvas.dataset.previewMax = String(this.previewMax);
        this.canvas.dataset.previewLit = String(this.previewLit);

        gl.useProgram(this.program);
        const pos = gl.getAttribLocation(this.program, "a_pos");
        gl.bindBuffer(gl.ARRAY_BUFFER, this.buffer);
        gl.enableVertexAttribArray(pos);
        gl.vertexAttribPointer(pos, 2, gl.FLOAT, false, 0, 0);

        gl.activeTexture(gl.TEXTURE0);
        gl.bindTexture(gl.TEXTURE_2D, this.texture);
        this.uniform1i("uLeds", 0);
        gl.activeTexture(gl.TEXTURE1);
        gl.bindTexture(gl.TEXTURE_2D, this.reflectionTexture);
        this.uniform1i("uReflections", 1);
        gl.activeTexture(gl.TEXTURE2);
        gl.bindTexture(gl.TEXTURE_2D, this.wallTexture);
        this.uniform1i("uWallLeds", 2);
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
        this.uniform1("uAtten", this.attenuation);
        this.uniform1("uCutoff", this.cutoff);
        this.uniform1("uBandHalf", (LED_DIAMETER_MM / 2) * SCALE);
        this.uniform1("uExposure", this.exposure);
        this.uniform1("uDichroic", this.dichroic);
        this.uniform1("uGlass", this.glass);
        this.uniform1("uShowRim", this.showRim ? 1.0 : 0.0);
        this.uniform1("uLedGain", this.ledGain);
        this.uniform1("uLedBlur", this.reflectionSpread / Math.max(1, this.ledCount));
        this.uniform1("uLedUvPitch", 1 / Math.max(1, this.ledCount));
        this.uniform1("uLedPitch", LED_PITCH_MM * SCALE);
        this.uniform1("uPixelWorld", 2 * geom.R * this.distance * Math.tan(35 * Math.PI / 360) /
            Math.max(1, this.canvas.height));
        this.uniform1("uGlow", this.glow);
        this.uniform1("uWallGlow", this.wallGlow);
        this.uniform1("uHasWall", this.wall ? 1.0 : 0.0);
        this.uniform1("uWallPlaneZ", -(geom.R + geom.wallDistance));
        this.uniform1("uWallDistance", geom.wallDistance);
        this.uniform1("uRoomHalf", geom.roomHalf);
        this.uniform1("uDiUpA", 0.00);
        this.uniform1("uDiUpB", 0.67);
        this.uniform1("uDiLoA", 0.17);
        this.uniform1("uDiLoB", 0.47);
        this.uniform1("uDiEdge", 0.35);
        this.uniform1("uDiBounce", 0.008);
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

    drawFallback(now, sampleTime = now / 1000) {
        drawHelioTracePreview(this.canvas, sampleTime, this.sampler, {
            ledCount: this.ledCount,
            background: "transparent",
            glow: true,
            dichroic: this.dichroic,
            glass: this.glass,
            showRim: this.showRim,
            ledFill: this.ledFill,
            ledGain: this.ledGain,
            reflectionSpread: this.reflectionSpread,
            glow: this.glow,
            wallGlow: this.wallGlow,
            wallDistanceCm: this.wallDistanceCm,
            offThreshold: this.offThreshold,
            offBrightness: this.offBrightness,
            attenuation: this.attenuation,
            cutoff: this.cutoff,
            wall: this.wall,
            tiltAnimation: this.tiltAnimation,
            swivelAnimation: this.swivelAnimation,
        });
    }
}

export const helioTrace = new HelioTrace({ interactive: true, bounces: 28, maxDpr: 0.95 });

const previewRenderers = new WeakMap();
let sharedPreviewRenderer = null;
let sharedPreviewCanvas = null;

export function helioTracePreviewTargetSize(canvas, maxDpr = 0.55, maxBackingSize = 0) {
    const dpr = renderDpr(maxDpr);
    const rect = canvas.getBoundingClientRect();
    const cssW = Math.max(1, Math.round(rect.width || canvas.clientWidth || canvas.width || 180));
    const cssH = Math.max(1, Math.round(rect.height || canvas.clientHeight || cssW));
    let w = Math.max(1, Math.round(cssW * dpr));
    let h = Math.max(1, Math.round(cssH * dpr));
    const cap = Math.max(0, Math.round(Number(maxBackingSize) || 0));
    const largest = Math.max(w, h);
    if (cap > 0 && largest > cap) {
        const scale = cap / largest;
        w = Math.max(1, Math.round(w * scale));
        h = Math.max(1, Math.round(h * scale));
    }
    if (canvas.width !== w || canvas.height !== h) {
        canvas.width = w;
        canvas.height = h;
    }
    return { cssW, cssH, w, h };
}

export function helioTracePreviewSampleCount(ledCount = DEFAULT_LED_COUNT) {
    return Math.max(256, Math.min(1536, Math.round(Number(ledCount) || DEFAULT_LED_COUNT)));
}

function ensureSharedPreviewRenderer() {
    if (!sharedPreviewCanvas) {
        sharedPreviewCanvas = document.createElement("canvas");
        Object.assign(sharedPreviewCanvas.style, {
            position: "fixed",
            left: "-10000px",
            top: "0",
            pointerEvents: "none",
            opacity: "0",
        });
        document.body.appendChild(sharedPreviewCanvas);
    }
    if (!sharedPreviewRenderer) {
        sharedPreviewRenderer = new HelioTrace({
            interactive: false,
            bounces: 8,
            maxDpr: 1.0,
            preserveDrawingBuffer: true,
        });
    }
    return { canvas: sharedPreviewCanvas, renderer: sharedPreviewRenderer };
}

export function drawHelioTraceWebGLPreview(canvas, t, sampler, opts = {}) {
    if (!canvas) return false;
    canvas.dataset.previewRenderer = "queued";
    canvas.dataset.previewMax = "0";
    canvas.dataset.previewLit = "0";
    if (opts.webgl === false) {
        drawHelioTracePreview(canvas, t, sampler, opts);
        return false;
    }
    if (opts.dedicated !== true) {
        try {
            const maxDpr = opts.maxDpr ?? 1.0;
            const target = helioTracePreviewTargetSize(canvas, maxDpr, opts.maxBackingSize);
            const effectiveDpr = Math.min(
                maxDpr,
                target.w / Math.max(1, target.cssW),
                target.h / Math.max(1, target.cssH),
            );
            const { canvas: source, renderer } = ensureSharedPreviewRenderer();
            source.style.width = `${target.cssW}px`;
            source.style.height = `${target.cssH}px`;
            if (source.width !== target.w || source.height !== target.h) {
                source.width = target.w;
                source.height = target.h;
            }
            renderer.setSampler(sampler);
            renderer.setLedCount(opts.ledCount || DEFAULT_LED_COUNT);
            renderer.setExposure(opts.exposure ?? 2.6);
            renderer.setDetail({
                bounces: opts.bounces ?? 8,
                maxDpr: effectiveDpr,
                dichroic: opts.dichroic,
                glass: opts.glass,
                showRim: opts.showRim,
                ledFill: opts.ledFill,
                ledGain: opts.ledGain,
                reflectionSpread: opts.reflectionSpread,
                glow: opts.glow,
                wallGlow: opts.wallGlow,
                wallDistanceCm: opts.wallDistanceCm,
                offThreshold: opts.offThreshold,
                offBrightness: opts.offBrightness,
                attenuation: opts.attenuation,
                cutoff: opts.cutoff,
                fps: opts.fps,
                wall: opts.wall,
                autoSpin: opts.autoSpin === true,
                tiltAnimation: opts.tiltAnimation === true,
                swivelAnimation: opts.swivelAnimation === true,
            });
            renderer.mount(source);
            renderer.draw(performance.now(), t);
            const ctx = canvas.getContext("2d");
            if (!ctx || !renderer.gl) throw new Error("preview snapshot unavailable");
            ctx.clearRect(0, 0, target.w, target.h);
            ctx.drawImage(source, 0, 0, target.w, target.h);
            canvas.dataset.previewRenderer = "heliotrace-webgl-shared";
            canvas.dataset.previewCamera = renderer.cameraAnimationMode();
            canvas.dataset.previewMax = String(renderer.previewMax);
            canvas.dataset.previewLit = String(renderer.previewLit);
            return true;
        } catch (err) {
            drawHelioTracePreview(canvas, t, sampler, opts);
            return false;
        }
    }
    let renderer = previewRenderers.get(canvas);
    if (!renderer) {
        renderer = new HelioTrace({ interactive: false, bounces: 12, maxDpr: 1.0 });
        previewRenderers.set(canvas, renderer);
    }
    renderer.setSampler(sampler);
    renderer.setLedCount(opts.ledCount || DEFAULT_LED_COUNT);
    renderer.setExposure(opts.exposure ?? 2.6);
    renderer.setDetail({
        bounces: opts.bounces ?? 12,
        maxDpr: opts.maxDpr ?? 1.0,
        dichroic: opts.dichroic,
        glass: opts.glass,
        showRim: opts.showRim,
        ledFill: opts.ledFill,
        ledGain: opts.ledGain,
        reflectionSpread: opts.reflectionSpread,
        glow: opts.glow,
        wallGlow: opts.wallGlow,
        wallDistanceCm: opts.wallDistanceCm,
        offThreshold: opts.offThreshold,
        offBrightness: opts.offBrightness,
        attenuation: opts.attenuation,
        cutoff: opts.cutoff,
        fps: opts.fps,
        wall: opts.wall,
        autoSpin: opts.autoSpin === true,
        tiltAnimation: opts.tiltAnimation === true,
        swivelAnimation: opts.swivelAnimation === true,
    });
    renderer.mount(canvas);
    renderer.draw(performance.now(), t);
    return !!renderer.gl;
}

export function drawHelioTracePreview(canvas, t, sampler, opts = {}) {
    if (!canvas) return;
    if (opts.fitCss !== false) helioTracePreviewTargetSize(canvas, opts.maxDpr ?? 1, opts.maxBackingSize);
    canvas.dataset.previewRenderer = "canvas";
    canvas.dataset.previewMax = "0";
    canvas.dataset.previewLit = "0";
    const ctx = canvas.getContext("2d");
    if (!ctx) {
        canvas.dataset.previewRenderer = "canvas-unavailable";
        return;
    }
    const w = canvas.width || 1;
    const h = canvas.height || 1;
    const cx = w / 2;
    const cy = h / 2;
    const r = Math.min(w, h) * (opts.radius || 0.39);
    const samples = helioTracePreviewSampleCount(opts.ledCount || DEFAULT_LED_COUNT);
    const sample = typeof sampler === "function" ? sampler : fallbackSample;
    const glow = clamp(Number(opts.glow) || DEFAULT_GLOW, 0.0, 3.0);
    const wallGlow = clamp(Number(opts.wallGlow) || DEFAULT_WALL_GLOW, 0.0, 3.0);
    const hasWall = opts.wall !== false;
    const offThreshold = clamp(finiteNumber(opts.offThreshold, DEFAULT_OFF_THRESHOLD), 0, 64);
    const offBrightness = clamp(finiteNumber(opts.offBrightness, DEFAULT_OFF_BRIGHTNESS), 0, 1);
    const colors = new Uint8Array(samples * 3);
    const brightness = new Uint8Array(samples);
    const reflectionColors = new Uint8Array(samples * 3);
    const wallColors = new Uint8Array(samples * 3);
    let previewMax = 0;
    let previewLit = 0;
    for (let i = 0; i < samples; i++) {
        const source = sample(i / samples, 0.5, t) || [0, 0, 0];
        const sourceMax = Math.max(clampByte(source[0]), clampByte(source[1]), clampByte(source[2]));
        const lit = sourceMax > offThreshold;
        const c = lit
            ? source
            : UNLIT_LED_RGB.map(channel => channel * offBrightness);
        const offset = i * 3;
        colors[offset] = clampByte(c[0]);
        colors[offset + 1] = clampByte(c[1]);
        colors[offset + 2] = clampByte(c[2]);
        const value = lit ? Math.max(colors[offset], colors[offset + 1], colors[offset + 2]) : 0;
        brightness[i] = value;
        previewMax = Math.max(previewMax, value);
        if (value > 24) previewLit++;
    }
    const reflectionSpread = clamp(Number(opts.reflectionSpread) || DEFAULT_REFLECTION_SPREAD, 0.0, 4.0);
    buildHelioReflectionColors(colors, brightness, offThreshold, reflectionColors);
    buildHelioWallColors(colors, brightness, offThreshold, wallColors);

    ctx.clearRect(0, 0, w, h);
    if (hasWall) {
        ctx.fillStyle = "#070506";
        ctx.fillRect(0, 0, w, h);
        ctx.save();
        ctx.globalCompositeOperation = "lighter";
        const diameterMm = inferDiameterMm(opts.ledCount || DEFAULT_LED_COUNT);
        const wallDistanceMm = clamp(finiteNumber(opts.wallDistanceCm,
            DEFAULT_WALL_DISTANCE_CM), 0, 200) * 10;
        const coneRadiusMm = (diameterMm + wallDistanceMm) * 1.428148;
        const spotRadius = Math.max(6, r * coneRadiusMm / (diameterMm * 0.5) / 2.5);
        for (let i = 0; i < samples; i++) {
            const offset = i * 3;
            const wallLevel = Math.max(wallColors[offset], wallColors[offset + 1], wallColors[offset + 2]);
            if (wallLevel === 0) continue;
            const angle = (i / samples) * TAU + 0.25 * TAU;
            const x = cx + Math.cos(angle) * r * 1.18;
            const y = cy + Math.sin(angle) * r * 0.80 - r * 0.04;
            const alpha = clamp((wallLevel / 255) * 0.026 * wallGlow, 0, 0.08);
            const spot = ctx.createRadialGradient(x, y, 0, x, y, spotRadius);
            spot.addColorStop(0, `rgba(${wallColors[offset]},${wallColors[offset + 1]},${wallColors[offset + 2]},${alpha})`);
            spot.addColorStop(1, `rgba(${wallColors[offset]},${wallColors[offset + 1]},${wallColors[offset + 2]},0)`);
            ctx.fillStyle = spot;
            ctx.fillRect(x - spotRadius, y - spotRadius, spotRadius * 2, spotRadius * 2);
        }
        ctx.restore();
        if (opts.showRim !== false) {
            const shadow = ctx.createLinearGradient(0, cy - r * 0.12, 0, cy + r * 0.12);
            shadow.addColorStop(0, "rgba(0,0,0,0)");
            shadow.addColorStop(0.5, "rgba(0,0,0,0.72)");
            shadow.addColorStop(1, "rgba(0,0,0,0)");
            ctx.fillStyle = shadow;
            ctx.fillRect(0, cy - r * 0.12, w, r * 0.24);
        }
    } else if (opts.background !== "transparent") {
        const bg = ctx.createLinearGradient(0, 0, w, h);
        bg.addColorStop(0, "#05070d");
        bg.addColorStop(0.58, "#0b101c");
        bg.addColorStop(1, "#080a12");
        ctx.fillStyle = bg;
        ctx.fillRect(0, 0, w, h);
    }

    const glassAlpha = clamp((Number(opts.glass) || 0) / 0.04, 0, 1);
    const shellAlpha = Math.max(glassAlpha * 0.55, opts.showRim === true ? 0.035 : 0);

    ctx.save();
    ctx.globalCompositeOperation = "lighter";
    ctx.translate(cx, cy);
    const scale = Math.min(w, h) / 350;
    const step = TAU / samples;
    const ledFill = clamp(Number(opts.ledFill) || DEFAULT_LED_FILL, 0.25, 1.35);
    const ledGain = clamp(Number(opts.ledGain) || DEFAULT_LED_GAIN, 0.5, 8.0);
    const haloRadius = (6.4 + reflectionSpread * 0.8) * scale;
    const midRadius = (3.6 + reflectionSpread * 0.4) * scale;
    const coreRadius = 1.6 * clamp(ledFill, 0.25, 1.35) * scale;
    for (let i = 0; i < samples; i++) {
        const offset = i * 3;
        const r8 = colors[offset];
        const g8 = colors[offset + 1];
        const b8 = colors[offset + 2];
        const sr8 = clampByte(r8 * ledGain);
        const sg8 = clampByte(g8 * ledGain);
        const sb8 = clampByte(b8 * ledGain);
        const oppositeIndex = (i + Math.floor(samples / 2)) % samples;
        const oppositeOffset = oppositeIndex * 3;
        const reflectedOffset = i * 3;
        const rr8 = clampByte(reflectionColors[reflectedOffset] * ledGain);
        const rg8 = clampByte(reflectionColors[reflectedOffset + 1] * ledGain);
        const rb8 = clampByte(reflectionColors[reflectedOffset + 2] * ledGain);
        const or8 = clampByte(reflectionColors[oppositeOffset] * ledGain);
        const og8 = clampByte(reflectionColors[oppositeOffset + 1] * ledGain);
        const ob8 = clampByte(reflectionColors[oppositeOffset + 2] * ledGain);
        const reflected = rr8 > 0 || rg8 > 0 || rb8 > 0 || or8 > 0 || og8 > 0 || ob8 > 0;
        for (let j = 0; reflected && j < PREVIEW_ARCS.length - 1; j++) {
            const useOpposite = (j & 1) === 0;
            const cr = useOpposite ? or8 : rr8;
            const cg = useOpposite ? og8 : rg8;
            const cb = useOpposite ? ob8 : rb8;
            const radius = PREVIEW_ARCS[j] * scale;
            ctx.fillStyle = `rgb(${cr},${cg},${cb})`;
            ctx.globalAlpha = clamp(0.15 * glow, 0.10, 0.34);
            ctx.beginPath();
            ctx.arc(radius, 0, haloRadius, 0, TAU);
            ctx.fill();
            ctx.globalAlpha = 0.28;
            ctx.beginPath();
            ctx.arc(radius, 0, midRadius, 0, TAU);
            ctx.fill();
            ctx.globalAlpha = 0.72;
            ctx.beginPath();
            ctx.arc(radius, 0, coreRadius, 0, TAU);
            ctx.fill();
        }
        const directRadius = PREVIEW_ARCS[PREVIEW_ARCS.length - 1] * scale;
        ctx.fillStyle = `rgb(${sr8},${sg8},${sb8})`;
        if (brightness[i] > 0) {
            ctx.globalAlpha = clamp(0.15 * glow, 0.10, 0.34);
            ctx.beginPath();
            ctx.arc(directRadius, 0, haloRadius, 0, TAU);
            ctx.fill();
            ctx.globalAlpha = 0.28;
            ctx.beginPath();
            ctx.arc(directRadius, 0, midRadius, 0, TAU);
            ctx.fill();
        }
        ctx.globalAlpha = 0.72;
        ctx.beginPath();
        ctx.arc(directRadius, 0, coreRadius, 0, TAU);
        ctx.fill();
        ctx.rotate(step);
    }
    canvas.dataset.previewRenderer = "canvas";
    canvas.dataset.previewCamera = opts.swivelAnimation === true
        ? "swivel"
        : opts.tiltAnimation === true ? "tilt" : "static";
    canvas.dataset.previewMax = String(previewMax);
    canvas.dataset.previewLit = String(previewLit);
    let reflectedSamples = 0;
    for (let i = 0; i < samples; i++) {
        const offset = i * 3;
        if (reflectionColors[offset] || reflectionColors[offset + 1] || reflectionColors[offset + 2]) reflectedSamples++;
    }
    canvas.dataset.previewReflections = String(reflectedSamples);
    ctx.restore();

    if (shellAlpha > 0) {
        const shine = ctx.createLinearGradient(cx - r, cy - r, cx + r, cy + r);
        shine.addColorStop(0, `rgba(255,255,255,${0.14 * shellAlpha})`);
        shine.addColorStop(0.2, `rgba(255,255,255,${0.01 * shellAlpha})`);
        shine.addColorStop(0.72, `rgba(255,188,92,${0.03 * shellAlpha})`);
        shine.addColorStop(1, `rgba(255,255,255,${0.08 * shellAlpha})`);
        ctx.strokeStyle = shine;
        ctx.lineWidth = Math.max(1, r * 0.035);
        ctx.beginPath();
        ctx.arc(cx, cy, r * 1.02, 0, TAU);
        ctx.stroke();

        ctx.fillStyle = `rgba(255,255,255,${0.12 * shellAlpha})`;
        ctx.beginPath();
        ctx.ellipse(cx - r * 0.28, cy - r * 0.38, r * 0.22, r * 0.08, -0.55, 0, TAU);
        ctx.fill();
    }
}
