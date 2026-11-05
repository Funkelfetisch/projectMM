#!/usr/bin/env python3
"""Exercise the firmware scan encoder against an app checkout's real TS decoder.

This is an offline cross-project compatibility check. It does not claim a radio
capture, a device identity, a deployed image or app hardware acceptance.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CPP = r'''
#include "core/util/BleAdvertisement.h"
#include "helio/BleAdvertisementRegistry.h"
#include <cstdio>
int main() {
    const int codes[] = {0,1,2,mm::ble_advertisement::productCodeForModel("NEBULITE Croptop S3"),
                         mm::ble_advertisement::productCodeForModel("NEBULITE Jacket S3"),
                         mm::ble_advertisement::productCodeForModel("HELIO necklace")};
    const int speeds[] = {-32768,-1,0,1,255,32767};
    for (int n = 0; n < 600; ++n) {
        mm::BleAdvertisementSnapshot v;
        v.product = {static_cast<uint8_t>(codes[n%6]), static_cast<uint8_t>((n%70)+1)};
        v.ledCount = static_cast<uint32_t>((n*79)%65536);
        v.powerKnown = n&1; v.powerOn = n&2;
        v.rgbwKnown = n&4; v.rgbw = n&8;
        v.animation = static_cast<uint8_t>(n%25);
        v.speedKnown = n&16; v.speed = speeds[n%6];
        v.rgbKnown = n&32; v.red = (n*29)%256; v.green = (n*71)%256; v.blue = (n*131)%256;
        v.whiteKnown = n&64; v.white = (n*17)%256;
        v.palette = n%61 == 60 ? 255 : static_cast<uint8_t>(n%60);
        auto a = mm::encodeBleAdvertisement(v);
        for (auto b : a) std::printf("%02x", b);
        std::printf("\n");
    }
    for (const auto& entry : mm::ble_advertisement::scenes) std::printf("scene|%u|%s\n", entry.code, entry.name);
    for (const auto& entry : mm::ble_advertisement::palettes) std::printf("palette|%u|%s\n", entry.code, entry.name);
}
'''
JS = r'''
import { decodeMoonlightAdvertisement, MOONLIGHT_AD_SCENES, MOONLIGHT_AD_PALETTES, MOONLIGHT_AD_PRODUCTS } from APP;
import { readFileSync } from 'node:fs';
const lines=readFileSync(process.argv[2],'utf8').trim().split('\n'), rows=lines.slice(0,600);
const codes=[0,1,2,20,22,3], speeds=[-32768,-1,0,1,255,32767];
const fail=(n,key,got,want)=>{throw new Error(`case ${n} ${key}: ${JSON.stringify(got)} != ${JSON.stringify(want)}`)};
for (let n=0;n<rows.length;n++) {
 const d=decodeMoonlightAdvertisement(Buffer.from(rows[n],'hex').toString('base64'));
 if(!d) fail(n,'decoder',null,'valid');
 const code=codes[n%6], rgbwKnown=Boolean(n&4), rgbw=Boolean(n&8), rgbKnown=Boolean(n&32);
 const expected={productCode:code,ledCount:(n*79)%65536||null,diameterCm:code===1||code===2?n%70+1:null,
  on:n&1?Boolean(n&2):null,rgbw:rgbwKnown?rgbw:null,animationCode:n%25,
  rawSpeed:n&16?speeds[n%6]:null,white:rgbwKnown&&rgbw&&(n&64)?(n*17)%256:null,
  paletteCode:n%61===60?255:n%60};
 for (const [key,value] of Object.entries(expected)) if(d[key]!==value) fail(n,key,d[key],value);
 const rgb=[(n*29)%256,(n*71)%256,(n*131)%256];
 const expectedRgb=rgbKnown?{r:Math.round((rgb[0]>>3)*255/31),g:Math.round((rgb[1]>>2)*255/63),b:Math.round((rgb[2]>>3)*255/31)}:null;
 if(JSON.stringify(d.colour)!==JSON.stringify(expectedRgb)) fail(n,'colour',d.colour,expectedRgb);
 const expectedKey={0:null,1:'HELIO1',2:'MINI80',3:'helio-necklace',20:'croptop',22:'jacket-vest'}[code];
 if((d.product?.key??null)!==expectedKey) fail(n,'product',d.product?.key??null,expectedKey);
 if(d.scene!==(MOONLIGHT_AD_SCENES[n%25]??null)) fail(n,'scene',d.scene,n%25);
 if(d.palette!==(MOONLIGHT_AD_PALETTES[expected.paletteCode]??null)) fail(n,'palette',d.palette,expected.paletteCode);
}
const registry={scene:MOONLIGHT_AD_SCENES,palette:MOONLIGHT_AD_PALETTES};
const seen={scene:0,palette:0};
for(const line of lines.slice(600)) {
 const [kind,key,name]=line.split('|');
 if(!registry[kind] || registry[kind][key]!==name) fail('registry',`${kind}:${key}`,registry[kind]?.[key],name);
 seen[kind]++;
}
for(const kind of Object.keys(registry)) if(seen[kind]!==Object.keys(registry[kind]).length) fail('registry',kind,seen[kind],Object.keys(registry[kind]).length);
if(rows.length!==600 || !MOONLIGHT_AD_PRODUCTS[20] || !MOONLIGHT_AD_PRODUCTS[22]) throw new Error('coverage');
console.log(`PASS: ${rows.length} firmware-encoded packets decoded by the app`);
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app-root', type=Path, required=True, help='NEBULITE app checkout')
    parser.add_argument('--esbuild', type=Path, required=True, help='installed esbuild executable')
    args = parser.parse_args()
    decoder = args.app_root.resolve() / 'apps/mobile/src/ble/moonlightAdvertisement.ts'
    if not decoder.is_file() or not args.esbuild.is_file():
        parser.error('app decoder or esbuild executable does not exist')
    compiler = shutil.which('c++')
    if not compiler or not shutil.which('node'):
        parser.error('a C++ compiler and node are required')
    with tempfile.TemporaryDirectory(prefix='moonlight-ad-compat-') as tmp:
        temp = Path(tmp)
        (temp / 'encoder.cpp').write_text(CPP)
        subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'),
                        str(temp / 'encoder.cpp'), '-o', str(temp / 'encoder')], check=True)
        packets = subprocess.check_output([str(temp / 'encoder')])
        (temp / 'packets.txt').write_bytes(packets)
        (temp / 'check.ts').write_text(JS.replace('APP', json.dumps(str(decoder)), 1))
        subprocess.run([str(args.esbuild.resolve()), str(temp / 'check.ts'), '--bundle', '--platform=node',
                        '--format=cjs', '--outfile=' + str(temp / 'check.cjs')], check=True, capture_output=True)
        subprocess.run(['node', str(temp / 'check.cjs'), str(temp / 'packets.txt')], check=True)


if __name__ == '__main__':
    main()
