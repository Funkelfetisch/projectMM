#!/usr/bin/env python3
"""Compare the actual app preview adapter with compiled firmware color functions.

Run offline with --app-root and --esbuild. Does not exercise BLE or LEDs.
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
const [count,first,stop,speed,size,ms] = process.argv.slice(2).map(Number);
const rgb = helioSceneExampleRgb({catalog:'0fd57205abff',scene:'nebulite-rainbow',
  controls:{'segment start':first,'segment stop':stop,speed,size},count,on:true,brightness:255},ms/1000);
if (!rgb) throw Error('App did not render the requested catalog scene');
console.log(JSON.stringify(Array.from({length:count},(_,i)=>Array.from(rgb.slice(i*3,i*3+3)))));
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app-root', type=Path, required=True)
    parser.add_argument('--esbuild', type=Path, required=True)
    args = parser.parse_args()
    adapter = args.app_root.resolve() / 'apps/mobile/src/preview/helioSceneExample.ts'
    reference = importlib.util.spec_from_file_location('rainbow_test', ROOT / 'test/python/test_nebulite_rainbow_preview.py')
    module = importlib.util.module_from_spec(reference)
    reference.loader.exec_module(module)
    checks = 0
    with tempfile.TemporaryDirectory(prefix='helio-app-rainbow-') as directory:
        temp = Path(directory)
        (temp / 'reference.cpp').write_text(module.CPP)
        (temp / 'app.ts').write_text(JS.replace('APP', json.dumps(str(adapter)), 1))
        subprocess.run(['g++', '-std=c++20', '-I', str(ROOT / 'src'),
                        '-I', str(ROOT / 'build/gcc-helio1/mm_core_root'),
                        '-I', str(ROOT / 'src/platform/desktop'), str(temp / 'reference.cpp'),
                        '-o', str(temp / 'reference')], check=True)
        subprocess.run([str(args.esbuild.resolve()), str(temp / 'app.ts'), '--bundle', '--platform=node',
                        '--format=cjs', '--outfile=' + str(temp / 'app.cjs')], check=True, capture_output=True)
        for count, first, stop in [(9, 0, 0), (91, 0, 31), (91, 3, 31), (91, 40, 31), (222, 0, 0)]:
            for speed, size, now in [(0, 0, 0), (128, 128, 0), (128, 128, 1999), (255, 255, 65535)]:
                values = [str(n) for n in (count, first, stop, speed, size, now)]
                expected = json.loads(subprocess.check_output([str(temp / 'reference'), *values]))
                actual = json.loads(subprocess.check_output(['node', str(temp / 'app.cjs'), *values]))
                if actual != expected:
                    raise SystemExit('preview mismatch: ' + ' '.join(values))
                checks += count
    print(f'{checks} ordered RGB pixels match across 20 firmware/app cases; no physical output tested')


if __name__ == '__main__':
    main()
