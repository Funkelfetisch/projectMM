#!/usr/bin/env python3
"""Compare Plasmoid's RGBW blending and ratio-preserving addition with WLED v0.9.

This checks color operations only. Motion, palette, scheduling and audio are separate.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--color-tools', type=Path, required=True)
args = parser.parse_args()
raw = args.color_tools.read_bytes()
if hashlib.sha1(b'blob ' + str(len(raw)).encode() + b'\0' + raw).hexdigest() != 'c47f1934684fde1d22e7e5801265d90db009f3ef':
    parser.error('colorTools.hpp differs from the pinned archived source')
source = raw.decode('utf-8')
first = source.index('inline __attribute__((hot,const)) uint32_t color_blend(')
last = source.index('inline __attribute__((hot,const)) uint32_t color_fade(', first)
functions = source[first:last]
code = r'''#include <cstdint>
#include <cstdio>
#include "light/effects/WledPlasmoidFrame.h"
using byte=uint8_t;
#define RGBW32(r,g,b,w) (uint32_t(byte(w))<<24|uint32_t(byte(r))<<16|uint32_t(byte(g))<<8|byte(b))
#define R(c) byte((c)>>16)
#define G(c) byte((c)>>8)
#define B(c) byte(c)
#define W(c) byte((c)>>24)
uint8_t qadd8(uint8_t a,uint8_t b){unsigned n=unsigned(a)+b;return n>255?255:n;}
''' + functions + r'''
uint32_t rng=7;
uint8_t next(){rng=rng*1664525u+1013904223u;return uint8_t(rng>>24);}
int main(){using F=mm::WledPlasmoidFrame; unsigned checked=0;
for(unsigned i=0;i<1000000;++i){
 F::Pixel a{next(),next(),next(),next()}, b{next(),next(),next(),next()};
 uint8_t t=next(); uint32_t x=RGBW32(a.r,a.g,a.b,a.w),y=RGBW32(b.r,b.g,b.b,b.w);
 auto got=F::add(a,b); uint32_t sum=color_add(x,y);
 if(R(sum)!=got.r||G(sum)!=got.g||B(sum)!=got.b||W(sum)!=got.w)return 1;
 uint32_t blended=color_blend(x,y,t);
 if(R(blended)!=mm::WledSineMotion::blend(a.r,b.r,t)||G(blended)!=mm::WledSineMotion::blend(a.g,b.g,t)||
 B(blended)!=mm::WledSineMotion::blend(a.b,b.b,t)||W(blended)!=mm::WledSineMotion::blend(a.w,b.w,t))return 2;
 ++checked;
}
mm::RGB palette[16] = {}; F::Pixel pixels[] = {{255,128,1,255}}; F frame;
frame.step(pixels,1,palette,{0,0,0,0},0,128,0,0);
if(pixels[0].r!=207||pixels[0].g!=104||pixels[0].b!=0||pixels[0].w!=0)return 3;
printf("matched %u archived RGBW additions and blends; checked 1D fade\n",checked);}
'''
with tempfile.TemporaryDirectory(prefix='wled-plasmoid-color-') as temp:
    src = Path(temp) / 'main.cpp'
    exe = Path(temp) / 'compare'
    src.write_text(code)
    subprocess.run(['g++', '-std=c++20', '-O2', '-I', str(ROOT / 'src'), str(src), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
