"""Compare Firefly browser frames with the firmware motion and palette."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CPP = r'''
#include "light/effects/WledTwinkleMotion.h"
#include "light/effects/WledFirePalette.h"
#include "light/effects/WledSineMotion.h"
#include <cstdio>
#include <cstdlib>
int main(int argc,char** argv) {
    if(argc!=7)return 2;
    unsigned count=std::strtoul(argv[1],nullptr,10), first=std::strtoul(argv[2],nullptr,10),
        stop=std::strtoul(argv[3],nullptr,10), speed=std::strtoul(argv[4],nullptr,10),
        intensity=std::strtoul(argv[5],nullptr,10), now=std::strtoul(argv[6],nullptr,10);
    if(first>count)first=count; if(!stop||stop>count)stop=count;
    mm::WledTwinkleMotion motion(now,speed,intensity);
    std::printf("[");
    for(unsigned i=0;i<count;++i){
        mm::RGB c{0,0,0};
        if(i>=first && i<stop){auto p=motion.next();auto target=mm::WledFirePalette::sample(p.paletteIndex);
            c={mm::WledSineMotion::blend(20,target.r,p.brightness),mm::WledSineMotion::blend(30,target.g,p.brightness),mm::WledSineMotion::blend(40,target.b,p.brightness)};}
        std::printf("%s[%u,%u,%u]",i?",":"",c.r,c.g,c.b);
    }
    std::printf("]");
}
'''
JS = r'''
import { readFileSync } from 'node:fs';
const url=s=>`data:text/javascript;base64,${Buffer.from(s).toString('base64')}`;
const data=readFileSync('src/ui/helio-scene-data.js','utf8');
const scenes=readFileSync('src/ui/helio-scenes.js','utf8').replaceAll('"./helio-scene-data.js"',JSON.stringify(url(data)));
const source=readFileSync('src/ui/helio-preview.js','utf8').replace('"./helio-scenes.js"',JSON.stringify(url(scenes)));
const {nebuliteFireflyPreviewColor}=await import(url(source));
const [count,first,stop,speed,intensity,ms]=process.argv.slice(2).map(Number);
const controls={'segment start':first,'segment stop':stop,speed,'effect intensity':intensity,
    'secondary red':20,'secondary green':30,'secondary blue':40,'frame ms':23};
const effect={controls:Object.entries(controls).map(([name,value])=>({name,value}))};
console.log(JSON.stringify(Array.from({length:count},(_,i)=>nebuliteFireflyPreviewColor(effect,ms/1000,(i+0.5)/count,count,{rgbOnly:true}))));
'''


class FireflyPreviewParity(unittest.TestCase):
    def test_frames_match_the_native_effect_primitives(self):
        with tempfile.TemporaryDirectory(prefix="firefly-parity-") as directory:
            tmp = Path(directory)
            (tmp / "ref.cpp").write_text(CPP)
            (tmp / "preview.mjs").write_text(JS)
            subprocess.run(["g++", "-std=c++20", "-I", str(ROOT / "src"), str(tmp / "ref.cpp"), "-o", str(tmp / "ref")], check=True)
            for count, first, stop in [(9, 0, 0), (91, 3, 31), (91, 40, 31), (222, 0, 0)]:
                for speed, intensity in [(0, 0), (55, 255), (255, 127)]:
                    for now in (0, 23, 1012):
                        args = [str(n) for n in (count, first, stop, speed, intensity, now)]
                        expected = json.loads(subprocess.check_output([str(tmp / "ref"), *args], cwd=ROOT))
                        actual = json.loads(subprocess.check_output(["node", str(tmp / "preview.mjs"), *args], cwd=ROOT))
                        self.assertEqual(actual, expected, args)
