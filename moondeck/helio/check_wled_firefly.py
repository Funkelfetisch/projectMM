#!/usr/bin/env python3
"""Compare Firefly with pinned archived WLED-MM and FastLED 3.6 sources.

This tests pre-driver values, not timing, transition state, hardware or optical output.
"""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--wled-root', type=Path, required=True, help='Pinned WLED repository checkout')
parser.add_argument('--fastled-cpp', type=Path, required=True, help='FastLED 3.6 colorutils.cpp')
parser.add_argument('--fastled-header', type=Path, required=True, help='FastLED 3.6 colorutils.h')
args = parser.parse_args()

def pinned(path, expected):
    data = path.read_bytes()
    digest = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
    if digest != expected:
        parser.error(f'{path} does not match its pinned Git blob')
    return data.decode('utf-8')

cc = pinned(args.fastled_cpp, '088ceb1e7876e8d1736e91ec2dcab6db5b22ba80')
h = pinned(args.fastled_header, '7ebf4e8e0635ba61f39da1fa91b7c75ef70b23f8')
p = pinned(args.wled_root / 'wled00/palettes.h', '3bd2a36eca6fba7fc30f3a04f09e9033c6be626e')
fx = pinned(args.wled_root / 'wled00/FX.cpp', '04a77701f20b0c5e68e8e1458aa06aec8cc32130')
math = pinned(args.wled_root / 'wled00/wled_math.cpp', 'bd5423389ac1ca22a32d28d78f825133117dbe65')
color = pinned(args.wled_root / 'wled00/colorTools.hpp', 'c47f1934684fde1d22e7e5801265d90db009f3ef')
fill=cc[cc.index('void fill_gradient_RGB( CRGB* leds,'):cc.index('\n#if 0',cc.index('void fill_gradient_RGB( CRGB* leds,'))]
start=h.index('    CRGBPalette16& loadDynamicGradientPalette('); end=h.index('\n    }',start)+6; load=h[start:end]
start=cc.index('CRGB ColorFromPalette( const CRGBPalette16&');end=cc.index('\nCRGB ColorFromPalette(',start+10); sample=cc[start:end]
start=p.index('const byte lava_gp[] PROGMEM = {');end=p.index('};',start)+2; palette=p[start:end].replace('const byte','const uint8_t').replace(' PROGMEM','')
code='''#include <cstdint>\n#include <cstdio>\n#include "light/effects/WledFirePalette.h"\n#define FASTLED_SCALE8_FIXED 1\n#define cleanup_R1()\nusing saccum87=int16_t; using accum88=uint16_t; using TDynamicRGBGradientPalette_bytes=const uint8_t*;\nunion TRGBGradientPaletteEntryUnion{struct{uint8_t index,r,g,b;};uint32_t dword;};\nstruct CRGB{ union{uint8_t r,red;};union{uint8_t g,green;};union{uint8_t b,blue;};CRGB():r(0),g(0),b(0){}CRGB(uint8_t r,uint8_t g,uint8_t b):r(r),g(g),b(b){} };\n'''+fill+'''\nstruct CRGBPalette16{CRGB entries[16];const CRGB& operator[](unsigned i)const{return entries[i];}\n'''+load+'''\n};\nenum TBlendType{NOBLEND,LINEARBLEND,LINEARBLEND_NOWRAP};\nuint8_t lsrX4(uint8_t x){return x>>4;} uint8_t map8(uint8_t x,uint8_t,uint8_t){return uint8_t((uint16_t(x)*240)>>8);} uint8_t scale8_LEAVING_R1_DIRTY(uint8_t x,uint8_t s){return uint8_t((uint16_t(x)*(uint16_t(s)+1))>>8);}\n'''+sample+'\n'+palette+'''\nint main(){CRGBPalette16 p;p.loadDynamicGradientPalette(lava_gp);for(unsigned i=0;i<256;i++){auto q=ColorFromPalette(p,uint8_t(i*241u>>8),255,LINEARBLEND);auto r=mm::WledFirePalette::sample(i);if(q.r!=r.r||q.g!=r.g||q.b!=r.b){printf("mismatch index=%u reference=%u,%u,%u port=%u,%u,%u\\n",i,q.r,q.g,q.b,r.r,r.g,r.b);return 1;}} puts("matched 256 RGB samples");}\n'''
base = code[:code.rindex('int main()')]
body=fx[fx.index('uint16_t mode_twinkleup(void)'):fx.index('static const char _data_FX_MODE_TWINKLEUP')]
sine=math[math.index('int16_t sin16_t('):math.index('uint8_t cos8_t(')]
a=color.index('uint32_t color_blend(');b=color.index('\n}',a)+2;blend=color[a:b]
extra='''\n#include "light/effects/WledTwinkleMotion.h"\n#include "light/effects/WledSineMotion.h"\n#include <algorithm>\nusing std::min;\n#define W(x) (((x)>>24)&255u)\n#define R(x) (((x)>>16)&255u)\n#define G(x) (((x)>>8)&255u)\n#define B(x) ((x)&255u)\n#define RGBW32(r,g,b,w) (uint32_t(w)<<24|uint32_t(r)<<16|uint32_t(g)<<8|uint32_t(b))\n'''+sine+'\n'+blend+'''\nCRGBPalette16 pal;static uint16_t seed;uint16_t random16_get_seed(){return seed;}void random16_set_seed(uint16_t x){seed=x;}uint8_t random8(){seed=uint16_t(seed*2053u+13849u);return uint8_t(uint8_t(seed)+uint8_t(seed>>8));}\nstruct Strip {uint32_t now;} strip;\nuint32_t secondary;struct Seg {uint8_t speed,intensity;uint32_t values[64];uint32_t color_from_palette(uint8_t i,bool,bool,int){auto c=ColorFromPalette(pal,uint8_t(uint16_t(i)*241u>>8),255,LINEARBLEND);return RGBW32(c.r,c.g,c.b,0);}void setPixelColor(int i,uint32_t x){values[i]=x;}} segment;\n#define SEGMENT segment\n#define SEGLEN 64\n#define SEGCOLOR(x) secondary\n#define PALETTE_SOLID_WRAP false\n#define FRAMETIME 23\n'''+body+'''\nint main(){pal.loadDynamicGradientPalette(lava_gp);uint32_t times[]={0,1,23,1000,268435455,268435456,0xffffffff};int speeds[]={0,55,128,255};int intensities[]={0,127,255};uint32_t backs[]={0,RGBW32(20,30,40,64),0xffffffff};unsigned n=0;for(auto now:times)for(auto sp:speeds)for(auto in:intensities)for(auto bg:backs){strip.now=now;segment.speed=sp;segment.intensity=in;secondary=bg;seed=1234;mode_twinkleup();mm::WledTwinkleMotion m(now,sp,in);for(int i=0;i<64;i++){auto x=m.next();auto c=mm::WledFirePalette::sample(x.paletteIndex);uint32_t result=RGBW32(mm::WledSineMotion::blend(R(bg),c.r,x.brightness),mm::WledSineMotion::blend(G(bg),c.g,x.brightness),mm::WledSineMotion::blend(B(bg),c.b,x.brightness),mm::WledSineMotion::blend(W(bg),0,x.brightness));if(result!=segment.values[i]){printf("mismatch %u %d %d %d\\n",now,sp,in,i);return 1;}++n;}}printf("matched %u RGBW pixels\\n",n);}\n'''
with tempfile.TemporaryDirectory() as d:
 f=Path(d)/'main.cpp';f.write_text(base+extra);e=Path(d)/'test';subprocess.run(['g++','-std=c++20','-O2','-I',str(ROOT / 'src'),str(f),'-o',str(e)],check=True);subprocess.run([str(e)],check=True)
