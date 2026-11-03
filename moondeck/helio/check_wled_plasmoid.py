#!/usr/bin/env python3
"""Compare Plasmoid motion with archived WLED and FastLED fixed-point code.

Palette, RGB blending, fading and live microphone calibration are not tested.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--fx-cpp', type=Path, required=True)
parser.add_argument('--util-cpp', type=Path, required=True)
parser.add_argument('--lib8tion-h', type=Path, required=True)
args = parser.parse_args()
def pinned(path, digest):
    raw = path.read_bytes()
    if hashlib.sha1(b'blob ' + str(len(raw)).encode() + b'\0' + raw).hexdigest() != digest:
        parser.error(f'{path} differs from its pinned blob')
    return raw.decode('utf-8')
fx = pinned(args.fx_cpp, '04a77701f20b0c5e68e8e1458aa06aec8cc32130')
util = pinned(args.util_cpp, '3b380ce8cb5e1628bbcd808c6284d3f2e34f2b76')
lib = pinned(args.lib8tion_h, 'f5916ccd8fb720d6477fdebe175698afff460292')
def section(text, begin, end):
    start = text.index(begin); return text[start:text.index(end, start)]
beats = section(lib, 'LIB8STATIC uint16_t beat88(', '\n\n/// Generates a 16-bit sine wave')
triangle = section(lib, 'LIB8STATIC uint8_t triwave8(', '\n\n/// Quadratic waveform')
cubic = section(lib, 'LIB8STATIC uint8_t cubicwave8(', '\n\n\n/// Square wave')
ease = section(lib, 'LIB8STATIC fract8 ease8InOutCubic(', '\n\n\n/// Fast, rough')
beat_sine = section(util, 'uint8_t beatsin8_t(', '\n\n///////////////////////////////////////////////////////////////////////////////')
mode = section(fx, 'typedef struct Plasphase', 'static const char _data_FX_MODE_PLASMOID')
code = r'''#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <vector>
#include "light/effects/WledPlasmoidMotion.h"
using accum88=uint16_t;using fract8=uint8_t;
#define LIB8STATIC static
uint32_t clockNow=0;
#define GET_MILLIS() clockNow
uint8_t scale8(uint8_t a,uint8_t b){return uint8_t(uint16_t(a)*(uint16_t(b)+1u)>>8);}
uint8_t scale8_LEAVING_R1_DIRTY(uint8_t a,uint8_t b){return scale8(a,b);}
uint8_t sin8_t(uint8_t x){return mm::WledWashingMotion::sine(x);}
uint8_t cos8_t(uint8_t x){return sin8_t(uint8_t(x+64));}
uint8_t beatsin8_t(accum88, uint8_t, uint8_t, uint32_t=0, uint8_t=0);
''' + beats + triangle + ease + cubic + beat_sine + r'''
struct um_data_t{void* u_data[1];};float volume=0;um_data_t audio{{&volume}};
um_data_t* getAudioData(){return &audio;}
struct Segment {uint8_t speed=0,intensity=0;std::vector<uint16_t> output;
 void setUpLeds(){} void fill(uint32_t){} void fadeToBlackBy(uint8_t){}
 uint32_t color_from_palette(uint8_t index,bool,bool,int){return index;}
 void addPixelColor(int i,uint32_t value){output[i]=uint16_t(value);}} segment;
struct Env {alignas(4) uint8_t bytes[4]={};uint8_t* data=bytes;unsigned call=0;bool allocateData(unsigned n){return n==sizeof(bytes);}} env;
uint32_t color_blend(uint32_t,uint32_t index,uint8_t amount){return (index<<8)|amount;}
uint16_t mode_static(){return 23;}
#define SEGMENT segment
#define SEGENV env
#define SEGLEN (segment.output.size())
#define SEGCOLOR(x) 0u
#define PALETTE_SOLID_WRAP false
#define BLACK 0u
#define FRAMETIME 23
''' + mode + r'''
int main(){unsigned checked=0;for(unsigned n:{2u,9u,31u,222u})for(unsigned speed:{0u,41u,128u,255u})for(unsigned intensity:{0u,112u,255u})for(float level:{0.f,1.f,31.5f,128.f,255.f}){
 env=Env{};env.data=env.bytes;mm::WledPlasmoidMotion actual;segment.speed=speed;segment.intensity=intensity;segment.output.assign(n,0);volume=level;
 for(uint32_t t:{0u,23u,12345u,0x7fffffffu,0xfffffff0u,9u}){clockNow=t;mode_plasmoid();++env.call;actual.advance(t);
 for(unsigned i=0;i<n;++i){auto p=actual.pixel(i,speed,intensity,level);uint16_t expected=uint16_t(p.paletteIndex<<8)|p.brightness;
 if(segment.output[i]!=expected){printf("mismatch n=%u speed=%u intensity=%u t=%u pixel=%u\n",n,speed,intensity,t,i);return 1;}++checked;}}}
 printf("matched %u Plasmoid index/brightness samples\n",checked);}
'''
with tempfile.TemporaryDirectory(prefix='wled-plasmoid-') as directory:
    source=Path(directory)/'main.cpp'; binary=Path(directory)/'compare'; source.write_text(code)
    subprocess.run(['g++','-std=c++20','-O2','-I',str(ROOT/'src'),str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
