#!/usr/bin/env python3
"""Check compiled firmware scene pages against an app checkout's real decoder.

Requires a current desktop GCC test build for the product. Offline only; this
neither exercises BLE hardware nor authenticates the built or installed image.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CPP = r'''
#include "helio/BleSceneCatalog.h"
#include "module_types.h"
#include <cstdio>
int main() {
    mm::registerModuleTypes();
    for (unsigned i = 0; i < mm::BleSceneCatalog::count(); ++i) {
        const auto& entry = mm::product_scenes::kEntries[i];
        std::printf("E {\"id\":\"%s\",\"controls\":%s}\n", entry.id, entry.controls);
        char buf[513]; mm::JsonSink s(buf,sizeof(buf)); s.append("{");
        if(mm::BleSceneCatalog::writePage(s,i)) return 1; s.append("}"); if(s.overflowed()) return 2;
        std::printf("S %s\n",buf);
        for (unsigned p=0;p<1024;++p) {
            char out[513]; mm::JsonSink r(out,sizeof(out)); r.append("{");
            if(const char* error=mm::BleSceneCatalog::writeInfo(r,entry.id,p)) {
                std::fprintf(stderr,"scene %s page %u: %s\n",entry.id,p,error); return 3;
            }
            r.append("}"); if(r.overflowed()) return 4;
            std::printf("I %s\n",out); mm::json::JsonDoc d;
            if(!mm::json::parse(out,d)) return 5;
            const auto* next=mm::json::member(d,d.rootNode(),"next"); if(!next) return 6;
            if(next->type==mm::json::JsonType::Null) break;
            if(next->intValue != static_cast<int>(p+1)) return 7;
            if(p==1023) return 8;
        }
    }
}
'''
JS = r'''
import {parseScenesPage, mergeSceneInfoPages} from APP;
import {readFileSync} from 'node:fs';
const rows=readFileSync(process.argv[2],'utf8').trim().split('\n');
let scenes=0, pages=0, catalog=null, groups=new Map(), expected=new Map(), failures=[];
for(const row of rows){
 const p=JSON.parse(row.slice(2));
 if(row.startsWith('E ')) expected.set(p.id,p.controls);
 else if(row.startsWith('S ')) {
  const s=parseScenesPage(p);scenes++;
  if(catalog===null)catalog=s.catalog;
  if(s.scenes.length!==1 || s.catalog!==catalog || s.scenes[0]?.id!==Array.from(expected.keys()).at(-1)) failures.push('scene page '+scenes);
 }
 else if(row.startsWith('I ')){pages++;const a=groups.get(p.scene)||[];a.push(p);groups.set(p.scene,a);}
 else throw Error('unknown row');
}
for(const [id,defaults] of expected){
 try {
  const info=mergeSceneInfoPages(groups.get(id));if(info.scene!==id||info.catalog!==catalog)throw Error('scene identity');
  for(const [name,value] of Object.entries(defaults)) {
   const control=info.controls.find(c=>c.name===name);
   if(!control||control.default!==value)throw Error(`default ${name}: ${control?.default} != ${value}`);
  }
  if(info.controls.length!==Object.keys(defaults).length)throw Error('unexpected control count');
 } catch(e) {failures.push(`${id}: ${String(e)}`);}
}
console.log(`Scenes: ${scenes}; metadata pages: ${pages}; complete sets: ${groups.size}; failures: ${failures.length}`);
for(const error of failures)console.error(error);
if(failures.length||scenes!==expected.size||groups.size!==expected.size||!scenes)process.exitCode=1;
'''


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--app-root', type=Path, required=True)
    p.add_argument('--esbuild', type=Path, required=True)
    p.add_argument('--build-dir', type=Path, default=ROOT / 'build/gcc-helio1')
    p.add_argument('--snapshot', type=Path, help='also verify every exported summary and default against native pages')
    args = p.parse_args()
    decoder = args.app_root.resolve() / 'apps/mobile/src/ble/moonlightScenes.ts'
    build = args.build_dir.resolve()
    objects = [build / 'CMakeFiles/MoonLight.dir/src/module_types.cpp.o',
               build / 'test/CMakeFiles/mm_tests.dir/__/src/module_types.cpp.o']
    object_file = next((path for path in objects if path.is_file()), objects[0])
    libs = [build / 'libmm_core.a', build / 'libmm_platform.a']
    compiler = shutil.which('c++')
    if not compiler or not shutil.which('node') or any(not path.is_file() for path in [decoder, args.esbuild, object_file, *libs]):
        p.error('a C++ compiler, node, esbuild, app decoder and desktop test build are required')
    with tempfile.TemporaryDirectory(prefix='moonlight-scene-compat-') as tmp:
        temp = Path(tmp)
        (temp / 'dump.cpp').write_text(CPP)
        subprocess.run([compiler, '-std=c++20', '-I', str(ROOT / 'src'), '-I', str(ROOT / 'src/platform/desktop'),
                        str(temp / 'dump.cpp'), str(object_file), '-Wl,--start-group', *map(str, libs),
                        '-Wl,--end-group', '-ldl', '-lpthread', '-o', str(temp / 'dump')], check=True)
        pages = subprocess.check_output([str(temp / 'dump')])
        (temp / 'pages.txt').write_bytes(pages)
        if args.snapshot:
            snapshot = json.loads(args.snapshot.read_text(encoding='utf-8'))
            rows = pages.decode('utf-8').splitlines()
            expected = {row['id']: row['controls'] for line in rows if line.startswith('E ')
                        for row in [json.loads(line[2:])]}
            summaries = [json.loads(line[2:]) for line in rows if line.startswith('S ')]
            published = snapshot.get('scenes')
            if not isinstance(published, list) or len(published) != len(summaries):
                raise SystemExit('snapshot scene count differs from native catalogue')
            for page, supplied in zip(summaries, published):
                actual = page['scenes'][0]
                if page['catalog'] != snapshot.get('revision') or any(supplied.get(k) != actual[k] for k in ('id', 'name', 'audio', 'rgbw')) or supplied.get('controls') != expected[actual['id']]:
                    raise SystemExit('snapshot differs from native scene ' + actual['id'])
            print(f'Snapshot: {len(published)} native summaries and default sets match')
        (temp / 'check.ts').write_text(JS.replace('APP', json.dumps(str(decoder)), 1))
        subprocess.run([str(args.esbuild.resolve()), str(temp / 'check.ts'), '--bundle', '--platform=node',
                        '--format=cjs', '--outfile=' + str(temp / 'check.cjs')], check=True, capture_output=True)
        subprocess.run(['node', str(temp / 'check.cjs'), str(temp / 'pages.txt')], check=True)


if __name__ == '__main__':
    main()
