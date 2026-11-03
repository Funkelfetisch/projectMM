#!/usr/bin/env python3
"""Compare Waterfall pixel shifting against the unchanged archived effect body.

Colour/palette calls use identical explicit adapters; this is not a colour or
live-microphone qualification.
"""
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
if hashlib.sha1(b'blob ' + str(len(raw)).encode() + b'\0' + raw).hexdigest() != '04a77701f20b0c5e68e8e1458aa06aec8cc32130':
    parser.error('FX.cpp differs from pinned WLED-MM source')
fx = raw.decode()
mode = fx[fx.index('uint16_t mode_waterfall(void)'):fx.index('static const char _data_FX_MODE_WATERFALL')]
program = r'''#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <initializer_list>
#include <vector>
#include "light/effects/WledWaterfallFrame.h"
using std::fill;
uint32_t clockNow=0; uint32_t micros(){return clockNow;}
uint8_t peak=0,bin=8,maximum=31; float frequency=200, magnitude=0;
struct um_data_t{void* u_data[8];} audio{{nullptr,nullptr,nullptr,&peak,&frequency,&magnitude,&maximum,&bin}};
um_data_t* getAudioData(){return &audio;}
struct Env {unsigned call=0; uint8_t aux0=0;} env;
struct Segment {
 uint8_t speed=0,intensity=0,custom1=0,custom2=0; std::vector<uint32_t> pixels;
 void setUpLeds(){} void fill(uint32_t p){std::fill(pixels.begin(),pixels.end(),p);}
 uint32_t getPixelColor(int i){return pixels.at(i);}
 void setPixelColor(int i,uint32_t p){pixels.at(i)=p;}
 uint32_t color_from_palette(uint8_t i,bool,bool,int){return 0x50000000u|i;}
} segment;
uint32_t CHSV(uint8_t h,uint8_t s,uint8_t v){return 0x30000000u|(uint32_t(h)<<16)|(uint32_t(s)<<8)|v;}
uint32_t color_blend(uint32_t a,uint32_t b,uint8_t alpha){return 0x90000000u|((a&255)<<16)|((b&255)<<8)|alpha;}
#define SEGMENT segment
#define SEGENV env
#define SEGLEN int(segment.pixels.size())
#define SEGCOLOR(x) 0x12345678u
#define BLACK 0u
#define PALETTE_SOLID_WRAP false
#define FRAMETIME 23
''' + mode + r'''
int main(){unsigned long compared=0;uint32_t rng=0x1234abcd;
 for(unsigned n:{1u,9u,31u,222u}) for(unsigned speed:{0u,41u,128u,254u,255u})
 for(unsigned intensity:{0u,112u,255u}) {
  segment=Segment{};env=Env{};segment.speed=speed;segment.intensity=intensity;
  segment.pixels.assign(n,0);std::vector<uint32_t> actual(n,0);mm::WledWaterfallFrame frame;
  for(unsigned i=0;i<600;++i){
   rng=rng*1664525u+1013904223u;clockNow=rng;peak=(rng>>28)==0;
   frequency=183.0f+float((rng>>8)%7000);magnitude=float((rng>>16)%2001);
   mode_waterfall();++env.call;
   frame.step(actual.data(),n,clockNow,speed,intensity,peak,frequency,uint8_t(int(magnitude/8)),
      0x12345678u,CHSV(92,92,92),[](uint8_t index){return 0x50000000u|index;},
      [](uint32_t a,uint32_t b,uint8_t v){return color_blend(a,b,v);});
   if(actual!=segment.pixels){std::printf("mismatch n=%u speed=%u frame=%u\n",n,speed,i);return 1;}
   compared+=n;
  }
 }
 std::printf("matched %lu adapted Waterfall output pixels\n",compared);
}
'''
with tempfile.TemporaryDirectory(prefix='wled-waterfall-frame-') as directory:
    source = Path(directory) / 'check.cpp'
    exe = Path(directory) / 'check'
    source.write_text(program)
    subprocess.run(['c++', '-std=c++20', '-O2', '-I', str(ROOT / 'src'), str(source), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
