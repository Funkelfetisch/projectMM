#!/usr/bin/env python3
"""Compare Flow Stripe hue with the pinned archived WLED mode, offline.

The reference uses the existing separately checked WLED sine adapter; it tests
the unchanged mode's hue math, not FastLED RGB conversion or device output.
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
    parser.error('FX.cpp does not match the pinned WLED blob')
fx = raw.decode('utf-8')
body = fx[fx.index('uint16_t mode_FlowStripe(void)'):fx.index('static const char _data_FX_MODE_FLOWSTRIPE')]
code = r'''#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "light/effects/WledFlowStripeMotion.h"
using byte=uint8_t;
struct Color {uint8_t h,s,v;};
Color CHSV(uint8_t h,uint8_t s,uint8_t v){return {h,s,v};}
struct Segment {uint8_t speed,intensity;std::vector<uint8_t> hue;void setPixelColor(int i,Color c){hue[i]=c.h;}} segment;
struct Strip {uint32_t now;} strip;
#define SEGMENT segment
#define SEGLEN (segment.hue.size())
#define FRAMETIME 23
uint8_t sin8_t(uint8_t value){return mm::WledWashingMotion::sine(value);}
''' + body + r'''
int main(){unsigned checked=0;for(unsigned n:{2u,9u,31u,222u,1024u})for(unsigned speed:{0u,1u,128u,255u})for(unsigned intensity:{0u,7u,8u,128u,255u})for(uint32_t now:{0u,1u,999u,65535u,0x7fffffffu,0xffffffffu}){
 segment.speed=speed;segment.intensity=intensity;segment.hue.assign(n,0);strip.now=now;
 mode_FlowStripe();for(unsigned i=0;i<n;i++){uint8_t value=0;
 if(!mm::WledFlowStripeMotion::hue(now,speed,intensity,i,n,value)||value!=segment.hue[i]){printf("mismatch n=%u i=%u time=%u\n",n,i,now);return 1;}++checked;}}
 uint8_t untouched=37;if(mm::WledFlowStripeMotion::hue(0,0,0,0,1,untouched)||untouched!=37)return 2;
 if(mm::WledFlowStripeMotion::hue(0,0,0,2,2,untouched))return 3;
 printf("matched %u original hue samples; guarded degenerate and out-of-range inputs\n",checked);}
'''
with tempfile.TemporaryDirectory(prefix='wled-flowstripe-') as directory:
    source = Path(directory) / 'main.cpp'; binary = Path(directory) / 'compare'
    source.write_text(code)
    subprocess.run(['g++', '-std=c++20', '-O2', '-I', str(ROOT / 'src'), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
