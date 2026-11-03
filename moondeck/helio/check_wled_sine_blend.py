#!/usr/bin/env python3
"""Compare the Sine channel blend against archived WLED-MM ESP32 fast-path code."""
import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
EXPECTED_BLOB = 'c47f1934684fde1d22e7e5801265d90db009f3ef'
PREAMBLE = r'''
#include "light/effects/WledSineMotion.h"
#include <cstdint>
#include <cstdio>
#define W(x) (((x)>>24)&255u)
#define R(x) (((x)>>16)&255u)
#define G(x) (((x)>>8)&255u)
#define B(x) ((x)&255u)
#define RGBW32(r,g,b,w) (uint32_t(w)<<24 | uint32_t(r)<<16 | uint32_t(g)<<8 | uint32_t(b))
'''
POSTAMBLE = r'''
int main(){
 unsigned count=0;
 for(unsigned a=0;a<256;++a) for(unsigned b=0;b<256;++b) for(unsigned t=0;t<256;++t){
  uint32_t x=RGBW32(a,a,a,a),y=RGBW32(b,b,b,b), old=color_blend(x,y,t,false);
  uint32_t n=mm::WledSineMotion::blend(a,b,t), current=RGBW32(n,n,n,n);
  if(old!=current){std::fprintf(stderr,"mismatch %u %u %u\n",a,b,t);return 1;} ++count;
 }
 std::printf("Archived WLED RGBW channel blend: %u combinations agree\n",count);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--colors', type=Path, required=True, help='Archived WLED-MM colorTools.hpp')
    args = parser.parse_args()
    raw = args.colors.read_bytes()
    blob = hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()
    if blob != EXPECTED_BLOB:
        parser.error('colorTools.hpp does not match the reviewed archived Git blob')
    source = raw.decode('utf-8')
    start = source.index('uint32_t color_blend(')
    brace = source.index('{', start)
    depth = 0
    for end in range(brace, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            break
    else:
        parser.error('unterminated color_blend')
    compiler = shutil.which('c++')
    if not compiler:
        parser.error('C++ compiler is required')
    with tempfile.TemporaryDirectory(prefix='wled-blend-reference-') as name:
        tmp = Path(name)
        (tmp/'reference.cpp').write_text(PREAMBLE+source[start:end+1]+POSTAMBLE)
        subprocess.run([compiler, '-std=c++20', '-O2', '-I', str(ROOT/'src'), str(tmp/'reference.cpp'), '-o', str(tmp/'reference')], check=True)
        subprocess.run([str(tmp/'reference')], check=True)


if __name__ == '__main__':
    main()
