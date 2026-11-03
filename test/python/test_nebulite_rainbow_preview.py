"""Compare actual firmware colour functions and the browser's strip preview."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CPP = r'''
#include "light/effects/NebuliteRainbowEffect.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    if (argc != 7) return 2;
    unsigned count = std::strtoul(argv[1], nullptr, 10);
    unsigned first = std::strtoul(argv[2], nullptr, 10);
    unsigned stop = std::strtoul(argv[3], nullptr, 10);
    unsigned speed = std::strtoul(argv[4], nullptr, 10);
    unsigned size = std::strtoul(argv[5], nullptr, 10);
    unsigned now = std::strtoul(argv[6], nullptr, 10);
    if (first > count) first = count;
    if (!stop || stop > count) stop = count;
    std::printf("[");
    for (unsigned i = 0; i < count; ++i) {
        mm::RGB c{0,0,0};
        if (i >= first && i < stop)
            c = mm::NebuliteRainbowEffect::rainbowColor(
                mm::NebuliteRainbowEffect::wheelIndex(now, speed, size, i-first, stop-first));
        std::printf("%s[%u,%u,%u]", i ? "," : "", c.r, c.g, c.b);
    }
    std::printf("]");
}
'''
JS = r'''
import { readFileSync } from 'node:fs';
const url = source => `data:text/javascript;base64,${Buffer.from(source).toString('base64')}`;
const data = readFileSync('src/ui/helio-scene-data.js', 'utf8');
const scenes = readFileSync('src/ui/helio-scenes.js', 'utf8').replaceAll('"./helio-scene-data.js"', JSON.stringify(url(data)));
const source = readFileSync('src/ui/helio-preview.js', 'utf8').replace('"./helio-scenes.js"', JSON.stringify(url(scenes)));
const { nebuliteRainbowPreviewColor } = await import(url(source));
const [count, first, stop, speed, size, ms] = process.argv.slice(2).map(Number);
const effect = {controls: Object.entries({'segment start': first, 'segment stop': stop, speed, size}).map(([name, value]) => ({name, value}))};
console.log(JSON.stringify(Array.from({length: count}, (_, i) => nebuliteRainbowPreviewColor(effect, ms/1000, (i+0.5)/count, count))));
'''


class RainbowPreviewParity(unittest.TestCase):
    def test_firmware_and_browser_on_wearable_and_helio_lengths(self):
        cases = [(9, 0, 0), (91, 0, 31), (91, 3, 31), (91, 40, 31), (222, 0, 0)]
        with tempfile.TemporaryDirectory(prefix="rainbow-parity-") as directory:
            tmp = Path(directory)
            (tmp / "ref.cpp").write_text(CPP)
            (tmp / "preview.mjs").write_text(JS)
            subprocess.run(["g++", "-std=c++20", "-I", str(ROOT / "src"), "-I", str(ROOT / "build/gcc-helio1/mm_core_root"), "-I", str(ROOT / "src/platform/desktop"), str(tmp / "ref.cpp"), "-o", str(tmp / "ref")], check=True)
            for count, first, stop in cases:
                for speed, size, now in [(0, 0, 0), (128, 128, 0), (128, 128, 1999), (255, 255, 65535)]:
                    args = [str(n) for n in (count, first, stop, speed, size, now)]
                    expected = json.loads(subprocess.check_output([str(tmp / "ref"), *args], cwd=ROOT))
                    actual = json.loads(subprocess.check_output(["node", str(tmp / "preview.mjs"), *args], cwd=ROOT))
                    self.assertEqual(actual, expected, args)


if __name__ == "__main__":
    unittest.main()
