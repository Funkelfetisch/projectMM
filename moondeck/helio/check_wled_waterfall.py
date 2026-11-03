#!/usr/bin/env python3
"""Compare Waterfall timing and defined colour indices with pinned WLED-MM."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--fx-cpp', type=Path, required=True)
args = parser.parse_args()
raw = args.fx_cpp.read_bytes()
digest = hashlib.sha1(b'blob ' + str(len(raw)).encode() + b'\0' + raw).hexdigest()
if digest != '04a77701f20b0c5e68e8e1458aa06aec8cc32130':
    parser.error('FX.cpp differs from the pinned WLED-MM source')
fx = raw.decode()
mode = fx[fx.index('uint16_t mode_waterfall(void)'):fx.index('static const char _data_FX_MODE_WATERFALL')]
timing = mode[mode.index('  uint8_t secondHand ='):mode.index('    //uint8_t pixCol')]
colour = mode[mode.index('    uint8_t pixCol ='):mode.index('    // loop will not execute')]
program = r'''#include <cstdint>
#include <cstdio>
#include <cmath>
#include "light/effects/WledWaterfallMotion.h"
struct Segment { uint8_t speed; } segment;
struct Env { uint8_t aux0; } env;
#define SEGMENT segment
#define SEGENV env
uint32_t clockNow;
uint32_t micros(){return clockNow;}
bool originalTiming() {
''' + timing + r'''
    return true;
  }
  return false;
}
uint8_t originalColour(float FFT_MajorPeak, uint8_t intensity) {
''' + colour + r'''
  return uint8_t(pixCol + intensity);
}
int main() {
  uint32_t rng=0x12345678; unsigned long count=0;
  for (unsigned speed=0; speed<256; ++speed) {
    mm::WledWaterfallMotion actual; env.aux0=255; segment.speed=speed;
    for(unsigned i=0;i<4096;++i) {
      rng=rng*1664525u+1013904223u; clockNow=rng;
      if(originalTiming()!=actual.advance(clockNow,speed)) return 1;
      ++count;
    }
  }
  // Only float-to-byte values in the standard's defined range are compared.
  for(unsigned f=183;f<=9000;++f) for(unsigned intensity=0;intensity<256;intensity+=17) {
    const float scaled=(std::log10(float(f))-2.26f)*150;
    if(scaled<0 || scaled>=256) continue;
    if(originalColour(float(f),intensity)!=mm::WledWaterfallMotion::paletteIndex(float(f),intensity)) return 2;
    ++count;
  }
  std::printf("matched %lu Waterfall timing and defined colour samples\n",count);
}
'''
with tempfile.TemporaryDirectory(prefix='wled-waterfall-') as directory:
    source = Path(directory) / 'check.cpp'
    exe = Path(directory) / 'check'
    source.write_text(program)
    subprocess.run(['c++', '-std=c++20', '-O2', '-I', str(ROOT / 'src'), str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
