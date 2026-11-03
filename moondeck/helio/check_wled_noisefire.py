#!/usr/bin/env python3
"""Compare archived Noisefire and FastLED palette sampling to the port.

The shared noise primitive is independently checked by check_wled_fastnoise.py.
Only valid original float-to-byte inputs are compared; microphone calibration,
the audio-free simulator and actual LED output require separate qualification.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--fx-cpp', type=Path, required=True)
p.add_argument('--colorutils-cpp', type=Path, required=True)
a = p.parse_args()

def pinned(path, digest):
    raw = path.read_bytes()
    if hashlib.sha1(b'blob ' + str(len(raw)).encode() + b'\0' + raw).hexdigest() != digest:
        p.error(f'{path} differs from its pinned blob')
    return raw.decode('utf-8')

def section(text, start, end):
    begin = text.index(start)
    return text[begin:text.index(end, begin)]

fx = pinned(a.fx_cpp, '04a77701f20b0c5e68e8e1458aa06aec8cc32130')
colors = pinned(a.colorutils_cpp, '088ceb1e7876e8d1736e91ec2dcab6db5b22ba80')
mode = section(fx, 'uint16_t mode_noisefire(void)', 'static const char _data_FX_MODE_NOISEFIRE')
palette = section(colors, 'CRGB ColorFromPalette( const CRGBPalette16&', 'CRGB ColorFromPalette( const TProgmemRGBPalette16&')
code = r'''#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <vector>
#include "light/effects/WledNoisefireFrame.h"
struct CHSV {uint8_t h,s,v;};
struct CRGB {
 union {uint8_t red; uint8_t r;}; union {uint8_t green; uint8_t g;}; union {uint8_t blue; uint8_t b;};
 CRGB(uint8_t r=0,uint8_t g=0,uint8_t b=0):red(r),green(g),blue(b){}
 CRGB(CHSV h) {auto c=mm::WledRandomPalette::rainbow({h.h,h.s,h.v});red=c.r;green=c.g;blue=c.b;}
 static const CRGB Red,DarkOrange,Orange,Yellow;
};
const CRGB CRGB::Red{255,0,0},CRGB::DarkOrange{255,140,0},CRGB::Orange{255,165,0},CRGB::Yellow{255,255,0};
struct CRGBPalette16 {CRGB entries[16]; template<class... T> CRGBPalette16(T... c):entries{CRGB(c)...}{} const CRGB& operator[](unsigned i)const{return entries[i];}};
enum TBlendType {NOBLEND,LINEARBLEND,LINEARBLEND_NOWRAP};
uint8_t map8(uint8_t x,uint8_t lo,uint8_t hi){return uint8_t(lo+((uint16_t(x)*(hi-lo+1))>>8));}
uint8_t lsrX4(uint8_t x){return x>>4;}
uint8_t scale8_LEAVING_R1_DIRTY(uint8_t a,uint8_t b){return uint8_t(uint16_t(a)*(uint16_t(b)+1)>>8);}
#define FASTLED_SCALE8_FIXED 1
#define cleanup_R1() ((void)0)
''' + palette + r'''
uint8_t inoise8(uint16_t x,uint16_t y){return mm::WledFastNoise::sample(x,y);}
struct um_data_t{void* u_data[1];}; float volume=0; um_data_t audio{{&volume}};
um_data_t* getAudioData(){return &audio;}
struct Segment {uint8_t speed=0,intensity=0;std::vector<CRGB> out;
 void fill(uint32_t){} void setPixelColor(int i, CRGB c){out[i]=c;}} segment;
struct Env {unsigned call=1;} env;
struct Strip {uint32_t now=0;} strip;
#define SEGMENT segment
#define SEGENV env
#define SEGLEN int(segment.out.size())
#define BLACK 0u
#define FRAMETIME 23
''' + mode + r'''
int main(){unsigned checked=0;
for(unsigned n:{1u,8u,9u,30u,31u,222u}) for(unsigned speed:{0u,1u,64u,128u,255u})
for(unsigned intensity:{0u,1u,128u,254u,255u}) for(float level:{0.f,0.5f,1.f,31.5f,64.f,127.5f}){
 segment.speed=speed;segment.intensity=intensity;segment.out.assign(n,{});volume=level;
 for(uint32_t t:{0u,23u,12345u,0x7fffffffu,0xfffffff0u}) {strip.now=t;mode_noisefire();
 for(unsigned i=0;i<n;++i){auto c=mm::WledNoisefireFrame::pixel(i,n,speed,intensity,t,uint8_t(level*2));auto ref=segment.out[i];
 if(c.r!=ref.r||c.g!=ref.g||c.b!=ref.b){printf("mismatch n=%u speed=%u intensity=%u t=%u pixel=%u\n",n,speed,intensity,t,i);return 1;}++checked;}}}
 printf("matched %u archived Noisefire pixels\n",checked);}
'''
with tempfile.TemporaryDirectory(prefix='wled-noisefire-') as directory:
    source = Path(directory) / 'main.cpp'
    binary = Path(directory) / 'compare'
    source.write_text(code)
    subprocess.run(['g++', '-std=c++20', '-O2', '-I', str(ROOT / 'src'), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
