#!/usr/bin/env python3
"""Compare the archived no-source BeatSin volume channel to the port."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--util-cpp', type=Path, required=True)
p.add_argument('--lib8tion-h', type=Path, required=True)
a = p.parse_args()
def pinned(path, digest):
    raw = path.read_bytes()
    if hashlib.sha1(b'blob ' + str(len(raw)).encode() + b'\0' + raw).hexdigest() != digest:
        p.error(f'{path} differs from pinned source')
    return raw.decode()
def section(text, start, end):
    at = text.index(start)
    return text[at:text.index(end, at)]
util = pinned(a.util_cpp, '3b380ce8cb5e1628bbcd808c6284d3f2e34f2b76')
lib = pinned(a.lib8tion_h, 'f5916ccd8fb720d6477fdebe175698afff460292')
beats = section(lib, 'LIB8STATIC uint16_t beat88(', '\n\n/// Generates a 16-bit sine wave')
sine = section(util, 'uint8_t beatsin8_t(', '\n\n///////////////////////////////////////////////////////////////////////////////')
peak = section(util, '  samplePeak    = random8() > 250;', '\n  zeroCrossingCount =')
program = r'''#include <cstdint>
#include <cstdio>
#include "light/effects/WledLegacyAudio.h"
using accum88=uint16_t;
#define LIB8STATIC static
uint32_t clockNow=0;
uint8_t randomValue=0;
uint8_t random8(){return randomValue;}
#define GET_MILLIS() clockNow
uint8_t scale8(uint8_t a,uint8_t b){return uint8_t(uint16_t(a)*(uint16_t(b)+1u)>>8);}
uint8_t sin8_t(uint8_t x){return mm::WledWashingMotion::sine(x);}
''' + beats + sine + r'''
void originalPeak(float volumeSmth, uint8_t& samplePeak, float& FFT_MajorPeak,
                  float& my_magnitude, uint8_t& maxVol, uint8_t& binNum,
                  uint16_t& volumeRaw) {
''' + peak + r'''
}
int main(){uint32_t state=0x13579bdf;for(unsigned i=0;i<100000;++i){
 state=state*1664525u+1013904223u;clockNow=state;
 if(beatsin8_t(120/(8+1),0,255,0,0)!=mm::WledLegacyAudio::volume(clockNow))return 1;}
 for(unsigned volume=0;volume<256;++volume)for(unsigned random=0;random<256;++random){
  randomValue=random; uint8_t samplePeak=0,maxVol=0,binNum=0;
  uint16_t volumeRaw=0; float frequency=0,magnitude=0;
  originalPeak(float(volume),samplePeak,frequency,magnitude,maxVol,binNum,volumeRaw);
  auto actual=mm::WledLegacyAudio::peak(volume,random);
  if(actual.peak!=bool(samplePeak)||actual.frequencyHz!=frequency||actual.magnitude!=magnitude) return 2;
 }
 puts("matched 100000 archived BeatSin volume samples and 65536 simulated peak inputs");}
'''
with tempfile.TemporaryDirectory(prefix='wled-audio-') as d:
    src=Path(d)/'check.cpp';exe=Path(d)/'check';src.write_text(program)
    subprocess.run(['c++','-std=c++20','-O2','-I',str(ROOT/'src'),str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
