#!/usr/bin/env python3
"""Pass real firmware pixel-map replies through the current App decoder and reader.

This verifies offline framing, reader, controller and preview geometry interoperability, not BLE or rendered UI.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CPP = r'''
#include "core/services/BleControlService.h"
#include "helio/BlePixelMapPage.h"
#include "helio/BleOutputPage.h"
#include "helio/BlePreviewFramePage.h"
#include "helio/BleAdvertisementRegistry.h"
#include "light/layouts/Layouts.h"
#include <cstdio>
#include <cstring>
int main(int argc, char** argv) {
  if (argc != 2 || std::strlen(argv[1]) >= sizeof(mm::PixelMapLayout::points)) return 1;
  mm::Scheduler scheduler;
  auto* layouts = new mm::Layouts(); layouts->setName("Layouts"); layouts->setTypeName("Layouts");
  auto* map = new mm::PixelMapLayout(); map->setName("ProductStrip"); map->setTypeName("PixelMapLayout");
  std::strcpy(map->points,argv[1]);
  layouts->addChild(map); scheduler.addModule(layouts);
  mm::BleControlService service; service.setScheduler(&scheduler); service.setPixelMapReader(&mm::BlePixelMapPage::writeCurrent); service.setOutputReader(&mm::BleOutputPage::writeCurrent); service.setPreviewFrameReader(&mm::BlePreviewFramePage::writeCurrent);
  service.setAdvertisingProduct({22, 0});
  service.setEpochForTest("00000000000000000000000000000001");
  service.handle(R"({"$v":1,"$id":1,"$op":"hello","$nonce":"00000000000000000000000000000002"})",100);
  char rev[33]; mm::sha256Hex(map->points,std::strlen(map->points),rev);
  service.handle(R"({"$v":1,"$id":2,"$op":"info","$e":"00000000000000000000000000000001","$s":1})",101); std::puts(service.response());
  service.handle(R"({"$v":1,"$id":3,"$op":"pixel-map","$e":"00000000000000000000000000000001","$s":1})",102); std::puts(service.response());
  char request[256]; std::snprintf(request,sizeof(request),"{\"$v\":1,\"$id\":4,\"$op\":\"pixel-map\",\"$e\":\"00000000000000000000000000000001\",\"$s\":1,\"$offset\":16,\"$rev\":\"%s\"}",rev);
  service.handle(request,103); std::puts(service.response());
  std::strcpy(map->points,"1,2;3,4"); std::snprintf(request,sizeof(request),"{\"$v\":1,\"$id\":5,\"$op\":\"pixel-map\",\"$e\":\"00000000000000000000000000000001\",\"$s\":1,\"$offset\":16,\"$rev\":\"%s\"}",rev);
  service.handle(request,104); std::puts(service.response());
  service.handle(R"({"$v":1,"$id":6,"$op":"pixel-map","$e":"00000000000000000000000000000001","$s":1})",105); std::puts(service.response());
  service.handle(R"({"$v":1,"$id":7,"$op":"profile","$e":"00000000000000000000000000000001","$s":1})",106); std::puts(service.response());
  scheduler.release();
}
'''

CLASSIC_CPP = CPP.replace('argc != 2', 'argc != 3').replace(
  'service.setAdvertisingProduct({22, 0});',
  'service.setAdvertisingProduct({mm::ble_advertisement::productCodeForModel(argv[2]), 0});').replace(
  'char request[256];', 'service.handle(R"({"$v":1,"$id":4,"$op":"profile","$e":"00000000000000000000000000000001","$s":1})",103); std::puts(service.response()); scheduler.release(); return 0; char request[256];')

CLASSIC_JS = r'''
import {parseMoonlightBleResponseHeader} from APP;
import {MoonlightBleController} from CONTROLLER;
import {configuredGarmentPoints, configuredGarmentProfile} from PREVIEW;
import {readFileSync} from 'node:fs';
const lines=readFileSync(process.argv[2],'utf8').trim().split('\n');
if(lines.length!==3)throw Error('classic response count');
const [info,page,actualProfile]=lines.map((line,i)=>{
 const header=parseMoonlightBleResponseHeader(new TextEncoder().encode(line));
 if(header.id!==i+2)throw Error('classic response id');return JSON.parse(line);
});
const expected=JSON.parse(readFileSync(process.argv[3],'utf8'));
if(!page.ok||page.map.total!==8||page.map.offset!==0||page.map.next!==null||
 JSON.stringify(page.map.points)!==JSON.stringify(expected.points))throw Error('classic map');
const profile={code:20,form:'garment'};
if(!actualProfile.ok||actualProfile.profile.code!==profile.code)throw Error('classic product profile');
const state:any={reportedProduct:{controllerId:'moonlight:34cdb0374ea4',configurationProfile:actualProfile.profile}};
if(configuredGarmentProfile(state)?.letter!=='C')throw Error('classic crop-top not selected');
let calls=0;
const controller:any=new MoonlightBleController({id:'test'} as any,{rpc:{request:async()=>{calls++;return page;}} as any});
controller.ready=true;controller.identity={extensions:{rpc:2}};controller.info=controller.parseInfo(info);
(async()=>{
 const map=await controller.readConfiguredPixelMap();
 if(calls!==1||JSON.stringify(map?.points)!==JSON.stringify(expected.points))throw Error('classic controller map');
 const display=configuredGarmentPoints(map,240);
 if(display.length!==8||display.some(p=>!Number.isFinite(p.x)||!Number.isFinite(p.y)))throw Error('classic preview geometry');
 console.log('Classic ESP32 crop-top: actual model code, app profile and eight ordered map points match.');
})().catch(error=>{console.error(error);process.exitCode=1;});
'''
JS = r'''
import {parseMoonlightBleResponseHeader} from APP;
import {readMoonlightConfiguredPixelMap} from READER;
import {MoonlightBleController} from CONTROLLER;
import {configuredGarmentPoints, configuredGarmentProfile} from PREVIEW;
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
const rows=readFileSync(process.argv[2],'utf8').trim().split('\n');
if(rows.length!==6)throw Error('row count');
const data=rows.map((text,i)=>{const bytes=new TextEncoder().encode(text);const h=parseMoonlightBleResponseHeader(bytes);if(h.id!==i+2)throw Error('id');return JSON.parse(text);});
const [info,a,b,stale,short,profile]=data;
if(info.pixelMapOp!==true||!a.ok||!b.ok||a.e!==b.e||a.s!==b.s||a.op!=='pixel-map'||b.op!=='pixel-map')throw Error('envelope');
if(a.map.revision!==b.map.revision||a.map.total!==31||b.map.total!==31||a.map.offset!==0||a.map.next!==16||b.map.offset!==16||b.map.next!==null)throw Error('paging');
const expected=JSON.parse(readFileSync(process.argv[3],'utf8'));
const points=[...a.map.points,...b.map.points];
if(JSON.stringify(points)!==JSON.stringify(expected.points))throw Error('UI and firmware points disagree');
if(a.map.revision!==createHash('sha256').update(expected.text).digest('hex').slice(0,32))throw Error('revision');
if(stale.ok!==false||stale.error!=='map-changed'||stale.id!==5)throw Error('stale');
const garmentState:any={reportedProduct:{controllerId:'moonlight:34cdb0374ea4',configurationProfile:profile.profile}};
if(!profile.ok||configuredGarmentProfile(garmentState)?.letter!=='J')throw Error('firmware profile does not select jacket preview');
if(configuredGarmentProfile({reportedProduct:{...garmentState.reportedProduct,controllerId:'unverified'}} as any)!==null)throw Error('uncorrelated profile selected preview');
async function run(sequence, expectedPoints, expectedRevision){
 let index=0; const requests=[];
 const result=await readMoonlightConfiguredPixelMap(async params=>{
  requests.push(params); if(index>=sequence.length)throw Error('unexpected page request');
  return sequence[index++];
 },()=>true);
 if(index!==sequence.length||JSON.stringify(result?.points)!==JSON.stringify(expectedPoints)||result?.revision!==expectedRevision)throw Error('app reader result');
 return requests;
}
(async()=>{
 const requests=await run([a,b], expected.points, a.map.revision);
 if(JSON.stringify(requests)!==JSON.stringify([{$offset:0},{$offset:16,$rev:a.map.revision}]))throw Error('app cursor');
 const restarted=await run([a,stale,short], [[1,2],[3,4]], short.map.revision);
 if(restarted[2].$offset!==0||Object.hasOwn(restarted[2],'$rev'))throw Error('app restart');
 let index=0; const calls:any[]=[]; let sequence=[a,b];
 const rpc={request:async(op:string,params:unknown)=>{calls.push({op,params});if(op!=='pixel-map'||index>=sequence.length)throw Error('unexpected controller request');return sequence[index++];}};
 const controller:any=new MoonlightBleController({id:'test'} as any,{rpc:rpc as any});
 // Exercise the real controller's serialized map read with simulated session setup.
 controller.ready=true; controller.identity={extensions:{rpc:2}}; controller.info=controller.parseInfo(info);
 if(!controller.supportsConfiguredPixelMap)throw Error('capability');
 const initial=await controller.readConfiguredPixelMap();
 if(index!==2||JSON.stringify(initial?.points)!==JSON.stringify(expected.points)||initial?.revision!==a.map.revision)throw Error('controller map');
 const display= configuredGarmentPoints(initial,240);
 if(display.length!==expected.points.length)throw Error('preview output count');
 const scale=(240-24)/20;
 for(let i=0;i<display.length;i++){
  if(Math.abs(display[i].x-(12+expected.points[i][0]*scale))>1e-8 ||
     Math.abs(display[i].y-(49.8+(expected.points[i][1]-1)*scale))>1e-8)throw Error(`preview point ${i}`);
 }
 index=0; calls.length=0; sequence=[a,stale,short];
 const actual=await controller.readConfiguredPixelMap();
 if(index!==3||JSON.stringify(actual?.points)!==JSON.stringify([[1,2],[3,4]])||actual?.revision!==short.map.revision)throw Error('controller restart');
 if(JSON.stringify(calls.map(c=>c.params))!==JSON.stringify([{$offset:0},{$offset:16,$rev:a.map.revision},{$offset:0}]))throw Error('controller cursor');
 controller.info=controller.parseInfo({...info,pixelMapOp:false}); calls.length=0;
 if(controller.supportsConfiguredPixelMap||await controller.readConfiguredPixelMap()!==null||calls.length)throw Error('unsupported capability');
 // A future additive info field must not break existing controls, although
 // this says nothing about whether the app understands or authorizes OTA.
 controller.info=controller.parseInfo({...info,experimentalUpdateProbe:true});
 if(!controller.supportsConfiguredPixelMap)throw Error('unknown info field broke existing capability');
 console.log('Actual firmware profile selects the App jacket preview; 31 ordered points and stale-map restart match.');
})().catch(error=>{console.error(error);process.exitCode=1;});
'''

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--app-root', type=Path, required=True)
    p.add_argument('--esbuild', type=Path, required=True)
    p.add_argument('--build-dir', type=Path, required=True)
    args = p.parse_args()
    decoder = args.app_root.resolve() / 'apps/mobile/src/ble/protocols/moonlightBle.ts'
    reader = args.app_root.resolve() / 'apps/mobile/src/ble/moonlightPixelMap.ts'
    controller = args.app_root.resolve() / 'apps/mobile/src/ble/controllers/moonlightBle.ts'
    preview = args.app_root.resolve() / 'apps/mobile/src/preview/configuredGarmentProfile.ts'
    libs = [args.build_dir.resolve() / 'libmm_core.a', args.build_dir.resolve() / 'libmm_platform.a']
    compiler = shutil.which('c++')
    if not compiler or not shutil.which('node') or any(not p.is_file() for p in [decoder, reader, controller, preview, args.esbuild, *libs]):
        p.error('a C++ compiler, node, esbuild, app decoder and reader, and desktop libraries are required')
    with tempfile.TemporaryDirectory(prefix='nebulite-map-compat-') as tmp:
        temp = Path(tmp)
        ui_script = r'''import fs from 'node:fs';
const load=async p=>import('data:text/javascript;base64,'+Buffer.from(fs.readFileSync(p)).toString('base64'));
const setup=await load(process.argv[1]);const data=await load(process.argv[2]);
const profile=data.NEBULITE_SETUP_PROFILES.find(p=>p.name==='NEBULITE Jacket S3');
const text=setup.productProjectionProfile(profile,'virtual2d').state.Layouts.ProductStrip.points;
console.log(JSON.stringify({text,points:setup.productProjectionPoints(profile).points}));'''
        expected = subprocess.check_output(['node', '--input-type=module', '-e', ui_script,
                                             str(ROOT / 'src/ui/nebulite-setup.js'), str(ROOT / 'src/ui/nebulite-profile-data.js')])
        (temp / 'expected.json').write_bytes(expected)
        point_text = json.loads(expected)['text']
        (temp / 'dump.cpp').write_text(CPP)
        subprocess.run([compiler, '-std=c++20', '-I', str(ROOT / 'src'), '-I', str(ROOT / 'src/platform/desktop'),
                        str(temp / 'dump.cpp'), '-Wl,--start-group', *map(str, libs), '-Wl,--end-group',
                        '-ldl', '-lpthread', '-o', str(temp / 'dump')], check=True)
        (temp / 'pages.txt').write_bytes(subprocess.check_output([str(temp / 'dump'), point_text]))
        (temp / 'check.ts').write_text(JS.replace('APP', json.dumps(str(decoder)), 1).replace('READER', json.dumps(str(reader)), 1).replace('CONTROLLER', json.dumps(str(controller)), 1).replace('PREVIEW', json.dumps(str(preview)), 1))
        subprocess.run([str(args.esbuild.resolve()), str(temp / 'check.ts'), '--bundle', '--platform=node',
                        '--format=cjs', '--external:expo-crypto', '--outfile=' + str(temp / 'check.cjs')], check=True, capture_output=True)
        subprocess.run(['node', str(temp / 'check.cjs'), str(temp / 'pages.txt'), str(temp / 'expected.json')], check=True)
        classic_ui_script = ui_script.replace('NEBULITE Jacket S3', 'NEBULITE Croptop ESP32')
        classic_expected = subprocess.check_output(['node', '--input-type=module', '-e', classic_ui_script,
                                                   str(ROOT / 'src/ui/nebulite-setup.js'), str(ROOT / 'src/ui/nebulite-profile-data.js')])
        (temp / 'classic-expected.json').write_bytes(classic_expected)
        (temp / 'classic.cpp').write_text(CLASSIC_CPP)
        subprocess.run([compiler, '-std=c++20', '-I', str(ROOT / 'src'), '-I', str(ROOT / 'src/platform/desktop'),
                        str(temp / 'classic.cpp'), '-Wl,--start-group', *map(str, libs), '-Wl,--end-group',
                        '-ldl', '-lpthread', '-o', str(temp / 'classic')], check=True)
        (temp / 'classic-pages.txt').write_bytes(subprocess.check_output([str(temp / 'classic'),
            json.loads(classic_expected)['text'], 'NEBULITE Croptop ESP32']))
        (temp / 'classic-check.ts').write_text(CLASSIC_JS.replace('APP', json.dumps(str(decoder)), 1)
            .replace('CONTROLLER', json.dumps(str(controller)), 1).replace('PREVIEW', json.dumps(str(preview)), 1))
        subprocess.run([str(args.esbuild.resolve()), str(temp / 'classic-check.ts'), '--bundle', '--platform=node',
                        '--format=cjs', '--external:expo-crypto', '--outfile=' + str(temp / 'classic-check.cjs')], check=True, capture_output=True)
        subprocess.run(['node', str(temp / 'classic-check.cjs'), str(temp / 'classic-pages.txt'), str(temp / 'classic-expected.json')], check=True)

if __name__ == '__main__':
    main()
