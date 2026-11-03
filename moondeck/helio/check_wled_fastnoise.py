#!/usr/bin/env python3
"""Compare the wearable noise adapter to the pinned FastLED 3.6 source."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--noise-cpp', type=Path, required=True)
parser.add_argument('--lib8tion-h', type=Path, required=True)
parser.add_argument('--math8-h', type=Path, required=True)
args = parser.parse_args()

def read_pinned(path, digest):
    data = path.read_bytes()
    if hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest() != digest:
        parser.error(f'{path} differs from its pinned FastLED blob')
    return data.decode('utf-8')

noise = read_pinned(args.noise_cpp, '56944f4d12161c8802224df2104ea70826c3bc44')
lib = read_pinned(args.lib8tion_h, 'f5916ccd8fb720d6477fdebe175698afff460292')
math = read_pinned(args.math8_h, '98e0adb2db70ba58dff0c8b920ad9a9d83df3ec9')
def section(text, start, end):
    at = text.index(start)
    return text[at:text.index(end, at)]

table = section(noise, 'FL_PROGMEM static uint8_t const p[] = {', '\n\n// Start Doxygen')
gradient = section(noise, 'static int8_t inline __attribute__((always_inline)) grad8(uint8_t hash, int8_t x, int8_t y)\n', '\nstatic int8_t inline __attribute__((always_inline)) grad8(uint8_t hash, int8_t x)')
lerp = section(noise, 'static int8_t inline __attribute__((always_inline)) lerp7by8(', '\nint16_t inoise16_raw(')
original = section(noise, 'int8_t inoise8_raw(uint16_t x, uint16_t y)\n', '// output range = -64')
ease = section(lib, 'LIB8STATIC uint8_t ease8InOutQuad( uint8_t i)', '\n#elif EASE8_AVRASM')
average = section(math, 'LIB8STATIC_ALWAYS_INLINE int8_t avg7(', '\n/// Calculate an integer average of two signed 15-bit')
code = r'''#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include "light/effects/WledFastNoise.h"
#include "core/util/noise.h"
using fract8=uint8_t;
#define FL_PROGMEM
#define FL_PGM_READ_BYTE_NEAR(p) (*(p))
#define LIB8STATIC static
#define LIB8STATIC_ALWAYS_INLINE static inline
#define AVG7_C 1
#define P(x) FL_PGM_READ_BYTE_NEAR(p + (x))
#define EASE8(x) ease8InOutQuad(x)
uint8_t scale8(uint8_t x,uint8_t y){return uint8_t(uint16_t(x)*(uint16_t(y)+1u)>>8);}
uint8_t qadd8(uint8_t x,uint8_t y){unsigned v=unsigned(x)+y;return v>255?255:v;}
''' + table + average + ease + gradient + lerp + original + r'''
int main(){for(unsigned x: {0u, 1u, 255u, 256u, 31108u})
 printf("reference (%u,19334)=%u\n",x,inoise8(x,19334));
uint32_t rng=0x92837465;unsigned differs=0;
for(unsigned i=0;i<1000000;++i){rng=rng*1664525u+1013904223u;uint16_t x=rng>>16;
 rng=rng*1664525u+1013904223u;uint16_t y=rng>>16;
 auto expected=inoise8(x,y),actual=mm::WledFastNoise::sample(x,y);
 if(expected!=actual){printf("mismatch %u %u: %u %u (raw %d)\n",x,y,expected,actual,inoise8_raw(x,y));return 1;}
 if(expected!=mm::inoise8(x,y))++differs;
}printf("matched 1000000 FastLED samples; MoonLight differs at %u\n",differs);}
'''
with tempfile.TemporaryDirectory(prefix='wled-fastnoise-') as directory:
    source = Path(directory) / 'main.cpp'; binary = Path(directory) / 'compare'; source.write_text(code)
    subprocess.run(['g++', '-std=c++20', '-O2', '-I', str(ROOT / 'src'), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
