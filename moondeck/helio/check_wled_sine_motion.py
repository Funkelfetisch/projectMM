#!/usr/bin/env python3
"""Compare the archived WLED Sine function against the native motion helper.

The original function is compiled unchanged with instrumented palette and
pixel callbacks. Palette generation, final RGBW blending, scheduling and real
LED output are outside this comparison.
"""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
EXPECTED_BLOB = '04a77701f20b0c5e68e8e1458aa06aec8cc32130'
PREAMBLE = r'''
#include "light/effects/WledSineMotion.h"
#include <cstdint>
#include <cstdio>
struct Segment {
 uint8_t speed=0, intensity=0; uint32_t result[64]{};
 uint32_t color_from_palette(uint32_t i, bool, bool, uint8_t) { return i & 255; }
 void setPixelColor(int i, uint32_t c) { result[i]=c; }
} segment;
struct Runtime { uint32_t step=0; } env;
struct Strip { uint32_t now=0; } strip;
#define SEGMENT segment
#define SEGENV env
#define SEGLEN 64
#define SEGCOLOR(i) 0
#define PALETTE_SOLID_WRAP false
#define FRAMETIME 23
uint8_t scale(uint8_t x,uint8_t y){return uint8_t(uint16_t(x)*(uint16_t(y)+1)>>8);}
uint8_t cubicwave8(uint8_t x){
 uint8_t tri=uint8_t((x&128?255-x:x)*2), square=scale(tri,tri), cube=scale(square,tri);
 uint16_t result=3u*square-2u*cube; return result&256?255:uint8_t(result);
}
uint32_t color_blend(uint32_t, uint32_t palette, uint8_t amount){return uint32_t(amount)<<8 | palette;}
'''
POSTAMBLE = r'''
int main(){
 const uint32_t times[]={0,31,32,16384,2097120,2097152,0xffffffffu};
 const uint8_t speeds[]={0,15,16,128,255}; unsigned cases=0;
 for(uint32_t now:times) for(uint8_t speed:speeds) for(unsigned intensity=0;intensity<256;++intensity){
  strip.now=now;segment.speed=speed;segment.intensity=uint8_t(intensity);env.step=0xfffffff9u;
  const uint32_t expectedStep=mm::WledSineMotion::advance(env.step,speed);
  if(mode_sinewave()!=23 || env.step!=expectedStep)return 1;
  for(unsigned i=0;i<64;++i){
   const uint32_t expected=uint32_t(mm::WledSineMotion::wave(expectedStep,i,uint8_t(intensity)))<<8 |
                           mm::WledSineMotion::paletteIndex(now,i);
   if(segment.result[i]!=expected){std::fprintf(stderr,"mismatch now=%u speed=%u intensity=%u pixel=%u\n",now,speed,intensity,i);return 2;}
   ++cases;
  }
 }
 std::printf("Archived WLED Sine motion/index: %u pixels agree\n",cases);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fx', type=Path, required=True, help='Archived WLED-MM FX.cpp')
    args = parser.parse_args()
    raw = args.fx.read_bytes()
    blob = hashlib.sha1(b'blob ' + str(len(raw)).encode() + b'\0' + raw).hexdigest()
    if blob != EXPECTED_BLOB:
        parser.error('FX.cpp does not match the reviewed archived Git blob')
    source = raw.decode('utf-8')
    marker = 'uint16_t mode_sinewave(void)'
    start = source.index(marker)
    brace = source.index('{', start)
    depth = 0
    for end in range(brace, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            break
    else:
        parser.error('unterminated effect function')
    compiler = shutil.which('c++')
    if not compiler:
        parser.error('C++ compiler is required')
    with tempfile.TemporaryDirectory(prefix='wled-sine-reference-') as name:
        tmp = Path(name)
        (tmp/'reference.cpp').write_text(PREAMBLE + source[start:end+1] + POSTAMBLE)
        subprocess.run([compiler, '-std=c++20', '-O2', '-I', str(ROOT/'src'), str(tmp/'reference.cpp'), '-o', str(tmp/'reference')], check=True)
        subprocess.run([str(tmp/'reference')], check=True)


if __name__ == '__main__':
    main()
