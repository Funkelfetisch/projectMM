#!/usr/bin/env python3
"""Compare our rainbow conversion with the unchanged FastLED 3.6 function.

The fixed-point scaling functions are explicit host shims, not an ESP32 binary.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--hsv-cpp', type=Path, required=True)
args = parser.parse_args()
raw = args.hsv_cpp.read_bytes()
if hashlib.sha1(b'blob ' + str(len(raw)).encode() + b'\0' + raw).hexdigest() != '0d094fd7f8d6e8b66f2acebd02f447d731d1b2f8':
    parser.error('hsv2rgb.cpp does not match the FastLED 3.6.0 blob')
text = raw.decode('utf-8')
start = text.index('void hsv2rgb_rainbow( const CHSV& hsv, CRGB& rgb)')
end = text.index('\n\nvoid hsv2rgb_raw(', start)
body = text[start:end]
code = r'''#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include "light/effects/WledRandomPalette.h"
#define FASTLED_SCALE8_FIXED 1
#define K255 255
#define K171 171
#define K170 170
#define K85 85
#define FORCE_REFERENCE(x) (void)(x)
#define cleanup_R1()
struct CHSV {uint8_t hue,sat,val;}; struct CRGB {uint8_t r,g,b;};
uint8_t scale8(uint8_t v,uint8_t s){return uint8_t(uint16_t(v)*(uint16_t(s)+1u)>>8);}
uint8_t scale8_video(uint8_t v,uint8_t s){return uint8_t((uint16_t(v)*s>>8)+(v&&s?1:0));}
uint8_t scale8_LEAVING_R1_DIRTY(uint8_t v,uint8_t s){return scale8(v,s);}
uint8_t scale8_video_LEAVING_R1_DIRTY(uint8_t v,uint8_t s){return scale8_video(v,s);}
''' + body + r'''
int main(){unsigned n=0;for(unsigned h=0;h<256;h++)for(unsigned s:{0u,1u,64u,128u,254u,255u})for(unsigned v:{0u,1u,64u,128u,254u,255u}){
 CRGB reference{};hsv2rgb_rainbow(CHSV{uint8_t(h),uint8_t(s),uint8_t(v)},reference);
 auto actual=mm::WledRandomPalette::rainbow({uint8_t(h),uint8_t(s),uint8_t(v)});
 if(reference.r!=actual.r||reference.g!=actual.g||reference.b!=actual.b){printf("mismatch h=%u s=%u v=%u\n",h,s,v);return 1;}++n;}
 printf("matched %u FastLED rainbow RGB conversions\n",n);}
'''
with tempfile.TemporaryDirectory(prefix='fastled-rainbow-') as directory:
    source = Path(directory) / 'main.cpp'; binary = Path(directory) / 'compare'
    source.write_text(code)
    subprocess.run(['g++', '-std=c++20', '-O2', '-I', str(ROOT / 'src'), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
