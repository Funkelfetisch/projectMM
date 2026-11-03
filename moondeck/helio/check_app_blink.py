#!/usr/bin/env python3
"""Compare the real app adapter with the firmware's blink palette and timing.

Offline only: does not exercise a BLE link, phone, output driver or LEDs.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
JS = r'''
import {helioSceneExampleRgb} from APP;
const [count,first,stop,blend,ms] = process.argv.slice(2).map(Number);
const rgb = helioSceneExampleRgb({catalog:'5022ee54a0b8',scene:'nebulite-blink',rgbOnly:true,
  controls:{'segment start':first,'segment stop':stop,'palette blend':blend,
    'secondary red':20,'secondary green':30,'secondary blue':40},count,on:true,brightness:255},ms/1000);
if (!rgb) throw Error('App did not render the requested catalog scene');
console.log(JSON.stringify(Array.from({length:count},(_,i)=>Array.from(rgb.slice(i*3,i*3+3)))));
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app-root', type=Path, required=True)
    parser.add_argument('--esbuild', type=Path, required=True)
    args = parser.parse_args()
    adapter = args.app_root.resolve() / 'apps/mobile/src/preview/helioSceneExample.ts'
    spec = importlib.util.spec_from_file_location('blink_test', ROOT / 'test/python/test_nebulite_blink_preview.py')
    reference = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(reference)
    checks = cases = 0
    with tempfile.TemporaryDirectory(prefix='helio-app-blink-') as directory:
        temp = Path(directory)
        (temp / 'reference.cpp').write_text(reference.CPP)
        (temp / 'app.ts').write_text(JS.replace('APP', json.dumps(str(adapter)), 1))
        subprocess.run(['g++', '-std=c++20', '-I', str(ROOT / 'src'), str(temp / 'reference.cpp'),
                        '-o', str(temp / 'reference')], check=True)
        subprocess.run([str(args.esbuild.resolve()), str(temp / 'app.ts'), '--bundle', '--platform=node',
                        '--format=cjs', '--outfile=' + str(temp / 'app.cjs')], check=True, capture_output=True)
        for count, first, stop in [(9, 0, 0), (91, 0, 31), (91, 3, 31), (91, 40, 31), (222, 0, 0)]:
            for blend in range(4):
                for now in (0, 273, 274, 546, 820):
                    values = [str(n) for n in (count, first, stop, blend, now)]
                    expected = json.loads(subprocess.check_output([str(temp / 'reference'), *values]))
                    actual = json.loads(subprocess.check_output(['node', str(temp / 'app.cjs'), *values]))
                    if actual != expected:
                        raise SystemExit('preview mismatch: ' + ' '.join(values))
                    checks += count
                    cases += 1
    print(f'{checks} ordered RGB pixels match across {cases} firmware/app cases; no physical output tested')


if __name__ == '__main__':
    main()
