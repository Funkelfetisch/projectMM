#!/usr/bin/env python3
"""Check real local ESP images with the App's actual image validator.

This does not authenticate a release, establish board compatibility, or flash.
"""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--app-root', type=Path, required=True)
parser.add_argument('--esbuild', type=Path, required=True)
parser.add_argument('images', type=Path, nargs='+')
args = parser.parse_args()
source = args.app_root.resolve() / 'apps/mobile/src/firmware/updateImage.ts'
script = r'''import {validateUpdateImage} from SOURCE;
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
const check=(bytes:Uint8Array, release:any)=>validateUpdateImage(bytes,release);
const hash=(bytes:Uint8Array)=>createHash('sha256').update(bytes).digest('hex');
for (const path of process.argv.slice(2)) {
  const b=new Uint8Array(readFileSync(path));
  const dv=new DataView(b.buffer,b.byteOffset,b.byteLength);
  const field=(offset:number,length:number)=>Buffer.from(b.slice(offset,offset+length)).toString('ascii').split('\0')[0];
  const r={size:b.length, sha256:hash(b), chipId:dv.getUint16(12,true),
    securityVersion:dv.getUint32(36,true),version:field(48,32),variant:field(296,32)};
  check(b,r);
  const rejected=(name:string, image:Uint8Array, release:any)=>{
    let failed=false; try { check(image,release); } catch { failed=true; }
    if(!failed) throw Error(`${path}: accepted ${name}`);
  };
  rejected('wrong chip',b,{...r,chipId:r.chipId===0?9:0});
  rejected('wrong variant',b,{...r,variant:'wrong-variant'});
  rejected('wrong security version',b,{...r,securityVersion:r.securityVersion+1});
  const changed=b.slice(); changed[changed.length-1]^=1;
  rejected('changed image with original digest',changed,r);
  console.log(`PASS ${path}: ${r.variant}, chip ${r.chipId}, ${r.size} bytes; wrong target and changed bytes rejected`);
}'''.replace('SOURCE', json.dumps(str(source)), 1)
with tempfile.TemporaryDirectory(prefix='helio-app-built-images-') as directory:
    tmp = Path(directory)
    (tmp / 'check.ts').write_text(script)
    subprocess.run([str(args.esbuild.resolve()), str(tmp / 'check.ts'), '--bundle', '--platform=node',
                    '--external:expo-crypto', '--outfile=' + str(tmp / 'check.cjs')], check=True)
    subprocess.run(['node', str(tmp / 'check.cjs'), *(str(p.resolve()) for p in args.images)], check=True)
