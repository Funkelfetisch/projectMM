import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

const source = readFileSync(new URL('../../src/ui/nebulite-live-preview.js', import.meta.url), 'utf8');
const { nebuliteLiveSampler } = await import(`data:text/javascript;base64,${Buffer.from(source).toString('base64')}`);
const frame = count => ({ count, stride: 1, rgb: Uint8Array.from({ length: count * 3 }, (_, i) => i),
    coords: Float32Array.from({ length: count * 3 }, (_, i) => i % 3 ? 0 : Math.floor(i / 3) / count) });

test('a complete ordered live source maps the crop-top and jacket logical positions', () => {
    for (const count of [9, 30]) {
        const f = frame(count), sample = nebuliteLiveSampler(f, count);
        assert.ok(sample);
        for (const i of [0, Math.floor(count / 2), count - 1])
            assert.deepEqual(sample((i + 0.5) / count), Array.from(f.rgb.slice(i * 3, i * 3 + 3)));
    }
});

test('coarsened, incomplete, reordered or non-linear source tables are not presented as a product preview', () => {
    const a = frame(9); a.stride = 2; assert.equal(nebuliteLiveSampler(a, 9), null);
    const b = frame(9); b.rgb = b.rgb.slice(0, -3); assert.equal(nebuliteLiveSampler(b, 9), null);
    const c = frame(9); c.coords[3] = c.coords[0]; assert.equal(nebuliteLiveSampler(c, 9), null);
    const d = frame(9); d.coords[10] = 0.2; assert.equal(nebuliteLiveSampler(d, 9), null);
    const e = frame(9); e.coords[9] += 0.01; assert.equal(nebuliteLiveSampler(e, 9), null);
    assert.equal(nebuliteLiveSampler(frame(8), 9), null);
});
test('complete row-major virtual grids preserve physical output order', () => {
    for (const [width, height] of [[3,3],[7,13]]) {
        const count = width * height, f = frame(count);
        f.coords = Float32Array.from({length:count*3}, (_, i) => i%3 === 0 ? Math.floor(i/3)%width/13 : i%3 === 1 ? Math.floor(Math.floor(i/3)/width)/13 : 0);
        const sample = nebuliteLiveSampler(f, count, width); assert.ok(sample);
        for (let i = 0; i < count; ++i) assert.deepEqual(sample((i+.5)/count), Array.from(f.rgb.slice(i*3,i*3+3)));
        f.coords[3] = f.coords[0]; assert.equal(nebuliteLiveSampler(f, count, width), null);
    }
});
test('sparse crop-top coordinates use the same ordered outputs as the garment simulator', () => {
    const points = [[1,8],[12,8],[10,3],[16,2],[4,0],[5,8],[13,4],[16,8],[8,4]], f = frame(points.length);
    f.coords = Float32Array.from(points.flatMap(([x,y])=>[x/17-.5,y/17-.3,0]));
    const sample = nebuliteLiveSampler(f, points.length, points); assert.ok(sample);
    for(let i=0;i<points.length;i++) assert.deepEqual(sample((i+.5)/points.length),Array.from(f.rgb.slice(i*3,i*3+3)));
    f.coords[7] += .01; assert.equal(nebuliteLiveSampler(f, points.length, points), null);
});
test('vertical and coincident maps still recognize complete ordered live frames', () => {
    for (const points of [[[3,0],[3,1],[3,2]], [[3,4],[3,4],[3,4]]]) {
        const f = frame(points.length);
        f.coords = Float32Array.from(points.flatMap(([x,y])=>[x/20-.3,y/20-.2,0]));
        const sample = nebuliteLiveSampler(f,points.length,points); assert.ok(sample);
        for(let i=0;i<points.length;i++) assert.deepEqual(sample((i+.5)/points.length),Array.from(f.rgb.slice(i*3,i*3+3)));
        f.coords[4] += .02; assert.equal(nebuliteLiveSampler(f,points.length,points),null);
    }
});
