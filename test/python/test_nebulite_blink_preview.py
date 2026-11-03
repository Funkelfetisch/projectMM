"""Compare the actual C++ palette and timing with the browser preview."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CPP = r'''
#include "light/effects/WledBlinkTiming.h"
#include "light/effects/WledTwoColorPalette.h"
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    if (argc != 6) return 2;
    unsigned count = std::strtoul(argv[1], nullptr, 10), first = std::strtoul(argv[2], nullptr, 10);
    unsigned stop = std::strtoul(argv[3], nullptr, 10), blend = std::strtoul(argv[4], nullptr, 10);
    unsigned now = std::strtoul(argv[5], nullptr, 10);
    if (first > count) first = count;
    if (!stop || stop > count) stop = count;
    mm::WledBlinkTiming phase;
    if (now >= 23) phase.next(now - 23, 230, 128, 23);
    bool on = phase.next(now, 230, 128, 23);
    const mm::RGB primary{0,0,255}, secondary{20,30,40};
    std::printf("[");
    for (unsigned i = 0; i < count; ++i) {
        mm::RGB c{0,0,0};
        if (i >= first && i < stop) c = on ? mm::WledTwoColorPalette::sample(primary, secondary, i-first, stop-first, blend) : secondary;
        std::printf("%s[%u,%u,%u]", i ? "," : "", c.r, c.g, c.b);
    }
    std::printf("]");
}
'''
JS = r'''
import { readFileSync } from 'node:fs';
const url = s => `data:text/javascript;base64,${Buffer.from(s).toString('base64')}`;
const data = readFileSync('src/ui/helio-scene-data.js', 'utf8');
const scenes = readFileSync('src/ui/helio-scenes.js', 'utf8').replaceAll('"./helio-scene-data.js"', JSON.stringify(url(data)));
const source = readFileSync('src/ui/helio-preview.js', 'utf8').replace('"./helio-scenes.js"', JSON.stringify(url(scenes)));
const { nebuliteBlinkPreviewColor } = await import(url(source));
const [count, first, stop, blend, ms] = process.argv.slice(2).map(Number);
const effect = {controls: Object.entries({'segment start': first, 'segment stop': stop, 'palette blend': blend,
  'secondary red':20,'secondary green':30,'secondary blue':40}).map(([name,value]) => ({name,value}))};
console.log(JSON.stringify(Array.from({length:count}, (_,i) => nebuliteBlinkPreviewColor(effect, ms/1000, (i+0.5)/count, count, {rgbOnly:true}))));
'''


class BlinkPreviewParity(unittest.TestCase):
    def test_nominal_frames_match_on_wearable_and_helio_lengths(self):
        with tempfile.TemporaryDirectory(prefix="blink-parity-") as directory:
            tmp = Path(directory)
            (tmp / "ref.cpp").write_text(CPP)
            (tmp / "preview.mjs").write_text(JS)
            subprocess.run(["g++", "-std=c++20", "-I", str(ROOT / "src"), str(tmp / "ref.cpp"), "-o", str(tmp / "ref")], check=True)
            for count, first, stop in [(9, 0, 0), (91, 0, 31), (91, 3, 31), (91, 40, 31), (222, 0, 0)]:
                for blend in range(4):
                    for now in (0, 273, 274, 546, 820):
                        args = [str(n) for n in (count, first, stop, blend, now)]
                        expected = json.loads(subprocess.check_output([str(tmp / "ref"), *args], cwd=ROOT))
                        actual = json.loads(subprocess.check_output(["node", str(tmp / "preview.mjs"), *args], cwd=ROOT))
                        self.assertEqual(actual, expected, args)


if __name__ == "__main__":
    unittest.main()
