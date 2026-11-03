#!/usr/bin/env python3
"""Compare the native wearable effect with the archived WLED Palette function.

The WLED palette callback is instrumented and resolved with the shared native
palette helper; color correction, scheduling on hardware and optical output
remain outside this comparison. Requires the current desktop static libraries.
"""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
EXPECTED_BLOB = '04a77701f20b0c5e68e8e1458aa06aec8cc32130'
PRE = r'''
#include "light/effects/NebulitePaletteEffect.h"
#include "light/layouts/GridLayout.h"
#include "light/layouts/Layouts.h"
#include <cstdint>
#include <cstdio>
struct Segment {
 uint8_t speed=0; unsigned length=0; uint8_t indexes[64]{}; bool wraps[64]{};
 uint32_t color_from_palette(uint8_t i,bool,bool wrap,uint8_t){return i | (uint32_t(wrap)<<8);}
 void setPixelColor(int i,uint32_t value){indexes[i]=uint8_t(value);wraps[i]=(value>>8)&1;}
} segment;
struct Strip { uint32_t now=0; uint8_t paletteBlend=0; } strip;
#define SEGMENT segment
#define SEGLEN segment.length
#define PALETTE_MOVING_WRAP !(strip.paletteBlend == 2 || (strip.paletteBlend == 0 && SEGMENT.speed == 0))
#define FRAMETIME 23
'''
POST = r'''
int main(){
 const uint32_t times[]={0,23,46,1000,65535,65558,0xfffffff0u,16};
 const uint8_t speeds[]={0,1,16,128,255}; unsigned checked=0;
 for(unsigned count: {9u,31u}) for(uint8_t speed:speeds){
  mm::Layouts layouts; mm::GridLayout grid; grid.width=count;grid.height=1;grid.depth=1;layouts.addChild(&grid);
  mm::Layer layer;layer.setLayouts(&layouts);layer.setChannelsPerLight(4);
  mm::NebulitePaletteEffect effect;effect.speed=speed;layer.addChild(&effect);layer.applyState();layer.tick();
  segment.length=count;segment.speed=speed;
  for(uint32_t now:times){
   strip.now=now;mode_palette();effect.renderAt(now);auto& frame=layer.buffer();
   for(unsigned i=0;i<count;++i){
    uint8_t idx=segment.indexes[i];if(!segment.wraps[i])idx=uint8_t(uint16_t(idx)*241u>>8);
    auto expected=mm::WledTwoColorPalette::sampleIndex({0,255,200},{255,0,255},idx,false);
    auto* p=frame.data()+4*i;
    if(p[0]!=expected.r||p[1]!=expected.g||p[2]!=expected.b||p[3]!=0){
      std::fprintf(stderr,"mismatch length=%u speed=%u now=%u pixel=%u\n",count,speed,now,i);return 1;
    } ++checked;
   }
  }
 }
 std::printf("Archived WLED palette motion/wrap: %u native RGBW pixels agree\n",checked);
}
'''


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fx',type=Path,required=True)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/gcc-nebulite-s3')
    args=parser.parse_args(); raw=args.fx.read_bytes()
    if hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()!=EXPECTED_BLOB:
        parser.error('FX.cpp does not match the reviewed archived Git blob')
    source=raw.decode('utf-8');start=source.index('uint16_t mode_palette()');brace=source.index('{',start);depth=0
    for end in range(brace,len(source)):
        depth+=(source[end]=='{')-(source[end]=='}')
        if not depth:break
    else:parser.error('unterminated effect')
    compiler=shutil.which('c++')
    if not compiler:parser.error('C++ compiler required')
    libs=[args.build_dir.resolve()/'libmm_core.a',args.build_dir.resolve()/'libmm_platform.a']
    with tempfile.TemporaryDirectory(prefix='wled-palette-reference-') as name:
        tmp=Path(name);(tmp/'reference.cpp').write_text(PRE+source[start:end+1]+POST)
        subprocess.run([compiler,'-std=c++20','-O2','-I',str(ROOT/'src'),'-I',str(ROOT/'src/platform/desktop'),str(tmp/'reference.cpp'),'-Wl,--start-group',*map(str,libs),'-Wl,--end-group','-ldl','-lpthread','-o',str(tmp/'reference')],check=True)
        subprocess.run([str(tmp/'reference')],check=True)


if __name__=='__main__':main()
