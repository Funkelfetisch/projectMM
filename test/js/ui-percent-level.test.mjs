// A 0-255 level that counts nothing, such as a brightness, shows as a percentage in both control renderers, while the slider and the device keep the native value.
//
// Run: `node --test test/js`.

import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { src, fnSource } from "./app-source.mjs";

const helio = readFileSync(new URL("../../src/ui/helio-app.js", import.meta.url), "utf8");

const f = new Function(`
    ${fnSource("levelShown")}
    ${fnSource("levelNative")}
    return { levelShown, levelNative };
`)();

test("a percent level shows 255 as 100, 128 as 50 and 0 as 0, and a plain number as itself", () => {
    assert.equal(f.levelShown(255, true), 100);
    assert.equal(f.levelShown(128, true), 50);
    assert.equal(f.levelShown(0, true), 0);
    assert.equal(f.levelShown(128, false), 128);
});

test("a percentage typed into a level's box writes the native value it stands for", () => {
    assert.equal(f.levelNative(100, true), 255);
    assert.equal(f.levelNative(50, true), 128);
    assert.equal(f.levelNative(0, true), 0);
    assert.equal(f.levelNative(42, false), 42);
    for (let native = 0; native <= 255; native++)
        assert.ok(Math.abs(f.levelNative(f.levelShown(native, true), true) - native) <= 2, `round trip of ${native}`);
});

for (const [name, code] of [["app.js", src], ["helio-app.js", helio]]) {
    test(`${name} renders a percent level's box and its live patch in percent`, () => {
        assert.match(code, /const percent = !!ctrl\.percent;/);
        assert.match(code, /numInput\.value = levelShown\(input\.value, percent\);/);
        assert.match(code, /levelNative\(parseInt\(numInput\.value\) \|\| 0, percent\)/);
        const at = code.indexOf("function updateModuleControls(");
        const patch = code.slice(at, code.indexOf('case "bool":', at));
        assert.match(patch, /val\.value = levelShown\(ctrl\.value \?\? 0, val\.dataset\.percent === "1"\)/);
    });
}
