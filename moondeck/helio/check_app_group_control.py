#!/usr/bin/env python3
"""Exercise real App group commands against real firmware dispatchers offline.

This uses local subprocess pipes instead of BLE; no physical device is tested.
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
#include "helio/BleSceneCatalog.h"
#include "light/drivers/Drivers.h"
#include "light/effects/SolidEffect.h"
#include "light/effects/PlasmaEffect.h"
#include "light/layers/Effects.h"
#include "light/layers/Layer.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <iostream>
int main(int argc, char** argv) {
 mm::ModuleFactory::registerType<mm::Layer>("Layer");
 mm::ModuleFactory::registerType<mm::Drivers>("Drivers");
 mm::ModuleFactory::registerType<mm::SolidEffect>("SolidEffect");
 mm::ModuleFactory::registerType<mm::PlasmaEffect>("PlasmaEffect");
 mm::Scheduler scheduler;
 auto *effects = new mm::Effects(); effects->setName("Effects"); effects->setTypeName("Effects");
 auto *layer = new mm::Layer(); layer->setName("Layer"); layer->setTypeName("Layer"); layer->markWiredByCode();
 effects->addChild(layer); effects->defineControls(); scheduler.addModule(effects);
 auto *drivers = new mm::Drivers(); drivers->setName("Drivers"); drivers->setTypeName("Drivers"); drivers->defineControls(); scheduler.addModule(drivers);
 mm::BleControlService service; service.setScheduler(&scheduler); service.setEpochForTest("00000000000000000000000000000001");
 if (argc != 2) return 2;
 service.setAdvertisingProduct({static_cast<uint8_t>(std::atoi(argv[1])), 0});
 static constexpr mm::BleSceneApi scenes{&mm::BleSceneCatalog::writePage,&mm::BleSceneCatalog::writeInfo,&mm::BleSceneCatalog::validate,&mm::BleSceneCatalog::apply,&mm::BleSceneCatalog::writeState}; service.setScenes(&scenes);
 service.handle(R"({"$v":1,"$id":1,"$op":"hello","$nonce":"00000000000000000000000000000002"})",0);
 std::string line; unsigned now=10;
 while(std::getline(std::cin,line)) { service.handle(line.c_str(),now++); std::cout << service.response() << std::endl; scheduler.preparePending(); service.applyDue(now); }
 scheduler.release();
}
'''
JS = r'''
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {MoonlightBleController} from CONTROLLER;
import {sendGroupCommand, commonEffects} from GROUPS;
import {GroupSession} from SESSION;
import {parseMoonlightConfigurationProfile} from PROFILE;
const epoch='00000000000000000000000000000001';
class Firmware {
 process:any;
 lines:any; id=1; requests:any[]=[]; failPatch=false; losePatchReply=false; sceneGate?:Promise<void>; sceneEntered?:()=>void;
 constructor(code:number){this.process=spawn(process.argv[2],[String(code)],{stdio:['pipe','pipe','inherit']});this.lines=createInterface({input:this.process.stdout})[Symbol.asyncIterator]();}
 async request(op:string,params:any={}) {
  if(op==='patch' && this.failPatch){this.failPatch=false;throw Error('simulated link failure');}
  const id=++this.id, wire={$v:1,$id:id,$op:op,$e:epoch,$s:1,...params};
  this.requests.push(wire); this.process.stdin.write(JSON.stringify(wire)+'\n');
  const line=await this.lines.next(); if(line.done)throw Error('firmware exited');
  const result=JSON.parse(line.value); if(result.id!==id||result.op!==op)throw Error('invalid response correlation');
  if(op==='patch' && this.losePatchReply){this.losePatchReply=false;throw Error('simulated lost reply after execution');}
  if(op==='scene' && this.sceneGate){const gate=this.sceneGate;this.sceneGate=undefined;this.sceneEntered?.();await gate;}
  return result;
 }
 async isConnected(){return true;}
 stop(){this.process.stdin.end();}
}
async function main(firstModel:string,secondModel:string){
 const firstCode=firstModel==='HELIO 1'?1:2;
 const code=secondModel==='NEBULITE Croptop ESP32'?20:secondModel==='HELIO MINI80'?2:22;
 const a=new Firmware(firstCode),b=new Firmware(code);
 try {
  const firmwareProfiles=await Promise.all([a,b].map(async rpc=>parseMoonlightConfigurationProfile((await rpc.request('profile')).profile)));
  if(firmwareProfiles[0]?.code!==firstCode||firmwareProfiles[0]?.form!=='sphere'||firmwareProfiles[0]?.verified!==false||firmwareProfiles[1]?.code!==code||firmwareProfiles[1]?.form!==(code===2?'sphere':'garment'))throw Error('firmware profile identity');
  const controllers=[a,b].map((rpc,i)=>{
    const c:any=new MoonlightBleController({id:`device-${i}`} as any,{rpc:rpc as any,wait:async()=>{}});
    // Synthetic product identities exercise mixed form factors; they do not
    // qualify MINI80 wiring or claim that either simulated output exists.
    c.ready=true; c.info={model:i===0?firstModel:secondModel,ledCount:i===0?(firstCode===1?222:MINI_COUNT):code===2?MINI_COUNT:code===20?8:31,channels:i===1&&code===20?3:MINI_CHANNELS,basChannels:0,basRestartRequired:false,profileOp:true,pixelMapOp:false};
    c.configurationProfile=firmwareProfiles[i];
    c.identity={firmware:'fixture',batteryOp:false}; return c;
  });
  await Promise.all(controllers.map(c=>sendGroupCommand(c,{type:'brightness',value:42})));
  await Promise.all(controllers.map(c=>sendGroupCommand(c,{type:'power',value:false})));
  for(const [i,c] of controllers.entries()){
    const state=await c.readState(); if(state.brightness!==42||state.on!==false)throw Error(`device ${i} state ${JSON.stringify(state)}`);
    const patches=[a,b][i].requests.filter(r=>r.$op==='patch');
    if(patches.length!==2||patches.some(r=>r.$live!==true))throw Error('unexpected group patch');
  }
  // Exercise the App's actual ownership, command and readback orchestration.
  // Connection setup is simulated; requests still reach each real dispatcher.
  for(const c of controllers){ c.connect=async()=>{}; c.disconnect=async()=>{}; }
  const devices=controllers.map(c=>({id:c.id,kind:'moonlight-ble',name:c.id}));
  const group=new GroupSession(device=>controllers.find(c=>c.id===device.id)!);
  await group.connect(devices as any);
  if(group.snapshot().phase!=='ready')throw Error(`group connection ${JSON.stringify(group.snapshot())}`);
  await group.command({type:'brightness',value:37});
  for(const c of controllers){const state=await c.readState();if(state.brightness!==37)throw Error('group brightness readback');}
  b.failPatch=true;
  let rejected=false;
  try { await group.command({type:'power',value:true}); } catch(error) {
    if(!String(error).includes('simulated link failure'))throw error;
    rejected=true;
  }
  if(!rejected)throw Error('group did not surface failed command');
  const members=group.snapshot().members;
  const failed=members.find(m=>m.device.id===controllers[1].id);
  if(!failed?.error||failed.state!==null)throw Error('failed member must report error and unknown power');
  if((await controllers[0].readState()).on!==true||(await controllers[1].readState()).on!==false)
    throw Error('partial group power change was not retained separately');
  // After an uncertain partial write the App requires a fresh group connection.
  if(group.snapshot().phase!=='error')throw Error('partial write did not stop the group');
  await group.disconnect();
  await group.connect(devices as any);
  if(group.snapshot().phase!=='ready')throw Error('group did not reconnect');
  // A later explicit retry must reach both dispatchers and clear the failed member's error.
  await group.command({type:'power',value:true});
  for(const c of controllers) if((await c.readState()).on!==true)throw Error('group retry did not converge');
  if(group.snapshot().members.some(m=>m.error))throw Error('group retry retained a member error');
  // The command can have taken effect even when its reply is lost. The group
  // must report uncertainty instead of implying that the member stayed on.
  b.losePatchReply=true;
  rejected=false;
  try { await group.command({type:'power',value:false}); } catch(error) {
    if(!String(error).includes('simulated lost reply after execution'))throw error;
    rejected=true;
  }
  if(!rejected || group.snapshot().phase!=='error')throw Error('lost reply did not stop the group');
  const uncertain=group.snapshot().members.find(m=>m.device.id===controllers[1].id);
  if(!uncertain?.error || uncertain.state!==null)throw Error('lost reply was not represented as unknown');
  for(const c of controllers)if((await c.readState()).on!==false)throw Error('lost reply fixture did not execute on both devices');
  const patchesBeforeReconnect=[a,b].map(peer=>peer.requests.filter(r=>r.$op==='patch').length);
  await group.disconnect();
  await Promise.all(controllers.map(c=>c.loadCatalogue(c.generation)));
  await group.connect(devices as any);
  if(group.snapshot().phase!=='ready')throw Error('group did not reconnect for effect');
  // Reconnection must replace the unknown result with readback, not assume
  // that the timed-out command failed or silently send it again.
  for(const member of group.snapshot().members)
    if(member.error || member.state?.on!==false)throw Error('group reconnect did not read back the applied command');
  if([a,b].some((peer,i)=>peer.requests.filter(r=>r.$op==='patch').length!==patchesBeforeReconnect[i]))
    throw Error('group reconnect repeated the uncertain power command');
  const profiles=group.snapshot().members.map(m=>m.profile!);
  const effect=commonEffects(profiles).find(item=>controllers.every(c=>item.targets[c.id]?.stableRef?.scene==='liquid-mirror'));
  if(!effect)throw Error('Liquid Mirror missing from real shared catalogue');
  await group.selectAutomaticEffect({type:'effect',key:effect.key,brightness:64,speed:5});
  for(const [i,c] of controllers.entries()){
    const state=await c.readState();
    if(state.brightness!==64)throw Error(`device ${i} brightness ${state.brightness}`);
    if(state.moonlightScene?.id!=='liquid-mirror')throw Error(`device ${i} scene ${JSON.stringify(state.moonlightScene)}`);
    const applied=[a,b][i].requests.filter(r=>r.$op==='scene');
    if(applied.length!==1||applied[0].$scene!=='liquid-mirror'||applied[0].$live!==true)throw Error('unexpected scene request');
  }
  // A manual choice uses the saved intent; the firmware sees a different flag
  // from an automatic live choice. This fixture does not simulate a restart.
  await group.selectEffect({type:'effect',key:effect.key,brightness:73,speed:5});
  for(const [i,c] of controllers.entries()){
    const state=await c.readState();
    if(state.brightness!==73||state.moonlightScene?.id!=='liquid-mirror')throw Error(`manual scene state on device ${i}`);
    const applied=[a,b][i].requests.filter(r=>r.$op==='scene');
    if(applied.length!==2||applied[1].$scene!=='liquid-mirror'||applied[1].$live!==false)throw Error('manual scene was not requested with saved intent');
  }
  // Hold an accepted response after the dispatcher executed it. Two later
  // choices must collapse to the latest while the accepted request settles.
  let release!:()=>void, entered!:()=>void;
  const waiting=new Promise<void>(resolve=>{entered=resolve;});
  a.sceneGate=new Promise<void>(resolve=>{release=resolve;}); a.sceneEntered=entered;
  const before=[a,b].map(peer=>peer.requests.length);
  const first=group.selectEffect({type:'effect',key:effect.key,brightness:41,speed:5});
  await waiting;
  const middle=group.selectEffect({type:'effect',key:effect.key,brightness:42,speed:5});
  const latest=group.selectEffect({type:'effect',key:effect.key,brightness:43,speed:5});
  release(); await Promise.all([first,middle,latest]);
  for(const [i,c] of controllers.entries()){
    const state=await c.readState();
    if(state.brightness!==43)throw Error(`latest selection not applied on device ${i}`);
    const burst=[a,b][i].requests.slice(before[i]);
    const scenes=burst.filter(r=>r.$op==='scene');
    const levels=burst.filter(r=>r.$op==='patch' && r.Drivers?.brightness!==undefined).map(r=>r.Drivers.brightness);
    if(scenes.length<1||scenes.length>2||levels.includes(42)||!levels.includes(43))
      throw Error(`selection burst did not preserve only the latest choice on device ${i}: ${JSON.stringify(levels)}`);
  }
  await group.disconnect();
  console.log(`Synthetic ${firstModel} and ${secondModel} identities: GroupSession, partial failure, shared catalogue and Liquid Mirror verified with two offline firmware dispatchers`);
 } finally { a.stop();b.stop(); }
}
async function threeProducts(){
 const specifications=[{code:1,model:'HELIO 1',count:222,channels:4},{code:2,model:'HELIO MINI80',count:MINI_COUNT,channels:MINI_CHANNELS},{code:22,model:'NEBULITE Jacket S3',count:30,channels:3}];
 const peers=specifications.map(p=>new Firmware(p.code));
 try {
  const controllers=await Promise.all(peers.map(async (rpc,i)=>{
   const p=specifications[i], profile=parseMoonlightConfigurationProfile((await rpc.request('profile')).profile);
   if(profile?.code!==p.code||profile.verified!==false)throw Error('unexpected product profile');
   const c:any=new MoonlightBleController({id:`three-${i}`} as any,{rpc:rpc as any,wait:async()=>{}});
   c.ready=true;c.info={model:p.model,ledCount:p.count,channels:p.channels,basChannels:0,basRestartRequired:false,profileOp:true,pixelMapOp:false};
   c.configurationProfile=profile;c.identity={firmware:'fixture',batteryOp:false};c.connect=async()=>{};c.disconnect=async()=>{};
   await c.loadCatalogue(c.generation);return c;
  }));
  const devices=controllers.map(c=>({id:c.id,kind:'moonlight-ble',name:c.id}));
  const group=new GroupSession(device=>controllers.find(c=>c.id===device.id)!);
  await group.connect(devices as any);
  if(group.snapshot().phase!=='ready'||group.snapshot().members.length!==3)throw Error('three product group not ready');
  await group.command({type:'brightness',value:55});
  const effect=commonEffects(group.snapshot().members.map(m=>m.profile!)).find(item=>controllers.every(c=>item.targets[c.id]?.stableRef?.scene==='liquid-mirror'));
  if(!effect)throw Error('three product shared scene missing');
  await group.selectEffect({type:'effect',key:effect.key,brightness:71,speed:5});
  for(const [i,c] of controllers.entries()){
   const state=await c.readState(), scenes=peers[i].requests.filter(r=>r.$op==='scene');
   if(state.brightness!==71||state.moonlightScene?.id!=='liquid-mirror'||scenes.length!==1||scenes[0].$live!==false)throw Error(`three product scene mismatch ${i}`);
  }
  await group.command({type:'power',value:true});
  peers[1].failPatch=true;
  let failure=false;
  try { await group.command({type:'power',value:false}); } catch(error) {
   if(!String(error).includes('simulated link failure'))throw error;
   failure=true;
  }
  if(!failure||group.snapshot().phase!=='error')throw Error('three member partial failure was not surfaced');
  const member=group.snapshot().members.find(m=>m.device.id===controllers[1].id);
  if(!member?.error||member.state!==null)throw Error('failed MINI80 result was not unknown');
  const states=await Promise.all(controllers.map(c=>c.readState()));
  if(states[0].on!==false||states[1].on!==true||states[2].on!==false)throw Error('three member partial state mismatch');
  const counts=peers.map(peer=>peer.requests.filter(r=>r.$op==='patch').length);
  await group.disconnect();await group.connect(devices as any);
  if(group.snapshot().phase!=='ready'||group.snapshot().members.some((m,i)=>m.error||m.state?.on!==states[i].on))throw Error('three member reconnect readback mismatch');
  if(peers.some((peer,i)=>peer.requests.filter(r=>r.$op==='patch').length!==counts[i]))throw Error('three member reconnect repeated a command');
  await group.disconnect();
  console.log('Synthetic HELIO 1, MINI80 and jacket: three-member scene, partial failure and reconnect verified with offline dispatchers');
 } finally { for(const peer of peers)peer.stop(); }
}
(async()=>{await main('HELIO MINI80','NEBULITE Jacket S3');await main('HELIO MINI80','NEBULITE Croptop ESP32');await main('HELIO 1','HELIO MINI80');await main('HELIO 1','NEBULITE Jacket S3');await threeProducts();})().catch(error=>{console.error(error);process.exitCode=1;});
'''

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--app-root',type=Path,required=True)
    p.add_argument('--esbuild',type=Path,required=True)
    p.add_argument('--build-dir',type=Path,default=ROOT/'build/gcc-nebulite-s3')
    args=p.parse_args()
    app=args.app_root.resolve()/'apps/mobile/src/ble'
    if not shutil.which('c++') or not shutil.which('node'): p.error('C++ compiler and node required')
    with tempfile.TemporaryDirectory(prefix='moonlight-group-compat-') as directory:
        temp=Path(directory)
        (temp/'firmware.cpp').write_text(CPP)
        libs=[args.build_dir.resolve()/'libmm_core.a',args.build_dir.resolve()/'libmm_platform.a']
        subprocess.run(['c++','-std=c++20','-I',str(ROOT/'src'),'-I',str(ROOT/'src/platform/desktop'),str(temp/'firmware.cpp'),'-Wl,--start-group',*map(str,libs),'-Wl,--end-group','-ldl','-lpthread','-o',str(temp/'firmware')],check=True)
        mini=json.loads((ROOT/'docs/reference/helio-mini80-profile.json').read_text())
        if not isinstance(mini.get('ledCount'),int) or not 1 <= mini['ledCount'] <= 512 or 'RGBW' not in mini.get('ledPart',''):
            raise RuntimeError('MINI80 source profile cannot define this synthetic RGBW fixture')
        js=JS.replace('CONTROLLER',json.dumps(str(app/'controllers/moonlightBle.ts')),1).replace('GROUPS',json.dumps(str(app/'groups.ts')),1).replace('SESSION',json.dumps(str(app/'groupSession.ts')),1).replace('PROFILE',json.dumps(str(app/'moonlightProductProfile.ts')),1).replace('MINI_COUNT',str(mini['ledCount'])).replace('MINI_CHANNELS','4')
        (temp/'app.ts').write_text(js)
        subprocess.run([str(args.esbuild.resolve()),str(temp/'app.ts'),'--bundle','--platform=node','--format=cjs','--external:expo-crypto','--loader:.png=empty','--outfile='+str(temp/'app.cjs')],check=True)
        subprocess.run(['node',str(temp/'app.cjs'),str(temp/'firmware')],check=True,timeout=30)

if __name__=='__main__': main()
