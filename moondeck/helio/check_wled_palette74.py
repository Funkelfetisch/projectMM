#!/usr/bin/env python3
"""Compare WLED-MM palette 74 transitions with pinned FastLED 3.6 code.

The fixture supplies deterministic palettes; random generation and device timing
are separate. The original FastLED blend function is compiled unchanged.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--fastled-cpp', type=Path, required=True)
args = parser.parse_args()
raw = args.fastled_cpp.read_bytes()
if hashlib.sha1(b'blob ' + str(len(raw)).encode() + b'\0' + raw).hexdigest() != '088ceb1e7876e8d1736e91ec2dcab6db5b22ba80':
    parser.error('colorutils.cpp does not match the FastLED 3.6 blob')
text = raw.decode('utf-8')
start = text.index('void nblendPaletteTowardPalette( CRGBPalette16& current')
end = text.index('\n\nuint8_t applyGamma_video', start)
body = text[start:end]
code = r'''#include <cstdint>
#include <cstdio>
#include "light/effects/WledRandomPalette.h"
struct CRGB {uint8_t r,g,b;}; struct CRGBPalette16 {CRGB entries[16];};
static_assert(sizeof(CRGBPalette16)==48);
''' + body + r'''
int main(){uint32_t rng=0x71e9a5u;unsigned checked=0;
for(unsigned sample=0;sample<40;++sample){mm::RGB old[16], next[16], actual[16];CRGBPalette16 before{},target{};
for(unsigned i=0;i<16;++i){auto draw=[&](){rng=rng*1664525u+1013904223u;return uint8_t(rng>>24);};
 old[i]={draw(),draw(),draw()};next[i]={draw(),draw(),draw()};before.entries[i]={old[i].r,old[i].g,old[i].b};target.entries[i]={next[i].r,next[i].g,next[i].b};}
for(unsigned ms=0;ms<=251;++ms){auto reference=before;
 if(ms<=250){for(unsigned i=0;i<128u*ms/250u;++i)nblendPaletteTowardPalette(reference,target,48);}else reference=target;
 mm::WledRandomPalette::transition74(old,next,ms,actual);
 for(unsigned i=0;i<16;++i){const auto r=reference.entries[i];const auto a=actual[i];
 if(r.r!=a.r||r.g!=a.g||r.b!=a.b){printf("mismatch sample=%u ms=%u slot=%u\n",sample,ms,i);return 1;}checked+=3;}}}
printf("matched %u palette channel values across the full transition\n",checked);}
'''
with tempfile.TemporaryDirectory(prefix='wled-palette74-') as directory:
    source = Path(directory) / 'main.cpp'; binary = Path(directory) / 'compare'
    source.write_text(code)
    subprocess.run(['g++', '-std=c++20', '-O2', '-I', str(ROOT / 'src'), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
