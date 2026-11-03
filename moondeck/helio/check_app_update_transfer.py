#!/usr/bin/env python3
"""Offline seam: real App update orchestration and firmware upload bridge.

All transport, authorization, storage and flash are simulated. The test wraps
App frames in experimental fragment packets; it does not test a BLE endpoint,
App packet encoding, signature verification, device boot or an actual update.
"""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CPP = r'''
#include "core/util/FirmwareUploadFragmentPacket.h"
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
struct Backend {
 std::vector<uint8_t> data;
 bool failCommit = false;
 size_t capacity() const {return 4096;}
 bool begin(size_t) {data.clear();return true;}
 bool write(const uint8_t* p,size_t n) {data.insert(data.end(),p,p+n);return true;}
 bool end(bool commit) {return !commit || !failCommit;}
};
int main(int argc, char**) {
 Backend backend; backend.failCommit = argc > 1;
 mm::FirmwareUploadFragmentBridge<Backend> bridge(backend,1000,10000,1000);
 std::string input; unsigned now=0; uint64_t owner=1;
 while(std::getline(std::cin,input)) {
  if(input=="disconnect") { bridge.disconnected(owner,++now); owner=2; std::cout << "{}" << std::endl; continue; }
  if(input=="expire") { now+=1001; bridge.tick(now); std::cout << "{}" << std::endl; continue; }
  std::vector<uint8_t> frame;
  for(size_t i=0;i+1<input.size();i+=2) frame.push_back(static_cast<uint8_t>(std::stoul(input.substr(i,2),nullptr,16)));
  decltype(bridge.append(1,0,0,nullptr,0,0)) out{};
  for(size_t pos=0;pos<frame.size();) {
   const size_t len=std::min<size_t>(16,frame.size()-pos);
   std::vector<uint8_t> packet={uint8_t(frame.size()),uint8_t(frame.size()>>8),uint8_t(pos),uint8_t(pos>>8)};
   packet.insert(packet.end(),frame.begin()+pos,frame.begin()+pos+len);
   out=mm::appendFirmwareUploadPacket(bridge,owner,packet.data(),packet.size(),++now);
   pos+=len;
   if(out.dispatched != (pos==frame.size())) return 2;
  }
  std::cout << "{\"dispatched\":" << (out.dispatched?"true":"false") << ",\"result\":" << int(out.outcome.result)
            << ",\"hasStatus\":" << (out.outcome.hasStatus?"true":"false") << ",\"phase\":" << int(out.outcome.status.phase)
            << ",\"received\":" << out.outcome.status.received << ",\"digest\":\"";
  if(out.outcome.hasStatus && out.outcome.status.hasVerifiedDigest) {
    static constexpr char hex[]="0123456789abcdef";
    for(auto b:out.outcome.status.verifiedDigest) std::cout << hex[b>>4] << hex[b&15];
  }
  std::cout << "\"}" << std::endl;
 }
}
'''
JS = r'''
import {spawn} from 'node:child_process';
import {createInterface} from 'node:readline';
import {createHash} from 'node:crypto';
import {FirmwareUpdate} from UPDATE;
const failCommit=process.argv[3]==='fail-commit';
const loseCommitReply=process.argv[3]==='lose-commit-reply';
const loseChunkReply=process.argv[3]==='lose-chunk-reply';
const loseBeginReply=process.argv[3]==='lose-begin-reply';
const child=spawn(process.argv[2],failCommit?['fail-commit']:[],{stdio:['pipe','pipe','inherit']});
const lines=createInterface({input:child.stdout})[Symbol.asyncIterator]();
let rejectedCommit:any=null;
async function control(command:string){child.stdin.write(command+'\n');const line=await lines.next();if(line.done)throw Error('firmware stopped');return JSON.parse(line.value);}
async function send(frame:Buffer){child.stdin.write(frame.toString('hex')+'\n');const line=await lines.next();if(line.done)throw Error('firmware stopped');const r=JSON.parse(line.value);if(frame[1]===3 && r.result!==0)rejectedCommit=r;if(!r.dispatched||r.result!==0||!r.hasStatus)throw Error('firmware rejected operation');return r;}
const bytes=new Uint8Array(336),v=new DataView(bytes.buffer);bytes[0]=0xe9;bytes[1]=1;v.setUint16(12,9,true);v.setUint32(28,296,true);v.setUint32(32,0xabcd5432,true);v.setUint32(36,2,true);
for(const [at,s] of [[48,'7.1.0'],[80,'MoonLight'],[288,'MMVAR01\0'],[296,'helio-s3']] as const)for(let i=0;i<s.length;i++)bytes[at+i]=s.charCodeAt(i);
bytes[335]=bytes.slice(32,328).reduce((n,b)=>n^b,0xef);
const hash=createHash('sha256').update(bytes).digest('hex'),tx='a'.repeat(32),deviceId='moonlight:112233445566';
// This intentionally synthetic authorization satisfies the App fixture only.
// It does not exercise a release signature or describe an actual device.
const pre={deviceId,bootId:'old',variant:'helio-s3',board:'fixture',partition:'fixture',chipId:9,slotBytes:4096,securityVersion:1,contractId:'OFFLINE-TEST-ONLY',transport:'ble',maxChunkBytes:100,deviceVerifiesSignature:true};
const release={releaseId:'fixture',url:'https://releases.example/app.bin',kind:'app-slot',variant:pre.variant,board:pre.board,partition:pre.partition,chipId:9,size:bytes.length,sha256:hash,version:'7.1.0',securityVersion:2,authorization:{signedBytes:[1,2],signature:[3,4],claims:{variant:pre.variant,board:pre.board,partition:pre.partition,chipId:9,size:bytes.length,securityVersion:2,sha256:hash}}};
let saved:any=null;let writes=0;let commits=0;let expiredObserved=false;
const receipt=(r:any,state:string)=>{
 const phase=state==='receiving'?0:state==='committed'?1:2;
 if(r.phase!==phase)throw Error('firmware phase does not match receipt');
 return {transactionId:tx,deviceId,state,offset:r.received,...(state==='committed'?{sha256:r.digest}:{})};
};
const ports:any={qualifiedContracts:['OFFLINE-TEST-ONLY'],acquire:async()=>()=>{},preflight:async()=>pre,authenticate:async()=>release,download:async()=>bytes,nonce:()=>tx,
 journal:{read:async()=>saved,write:async(j:any)=>{saved=j;},clear:async()=>{saved=null;}},
 begin:async()=>{const frame=Buffer.alloc(54);frame[0]=1;frame[1]=1;frame.writeUInt32LE(bytes.length,2);Buffer.from(hash,'hex').copy(frame,6);Buffer.from(tx,'hex').copy(frame,38);const confirmed=receipt(await send(frame),'receiving');if(loseBeginReply)throw Error('begin reply lost');return confirmed;},
 write:async(_t:string,offset:number,b:Uint8Array)=>{writes++;const frame=Buffer.alloc(6+b.length);frame[0]=1;frame[1]=2;frame.writeUInt32LE(offset,2);Buffer.from(b).copy(frame,6);const confirmed=receipt(await send(frame),'receiving');if(loseChunkReply&&writes===2)throw Error('chunk reply lost');return confirmed;},
 commit:async()=>{commits++;const confirmed=receipt(await send(Buffer.from([1,3])),'committed');if(loseCommitReply)throw Error('commit reply lost');return confirmed;},
 abort:async()=>receipt(await send(Buffer.from([1,4])),'aborted'),status:async()=>{
  const r=await send(Buffer.concat([Buffer.from([1,5]),Buffer.from(tx,'hex')]));
  if(r.phase!==2||r.received!==200)throw Error('firmware did not retain expired transfer status');
  expiredObserved=true;
  return {state:'unknown'}; // Firmware terminal evidence is volatile, not the durable App contract.
 },
 reconnect:async()=>{throw Error('no real boot evidence');}};
async function main(){try{const app=new FirmwareUpdate(ports);await app.initialize();await app.check('fixture');if(app.getSnapshot().phase!=='ready')throw Error('app did not accept test image');await app.install();const state=app.getSnapshot();if(loseBeginReply){if(state.phase!=='recovery-required'||state.confirmedBytes!==0||writes!==0||commits!==0||!saved)throw Error('lost begin reply did not retain journal');const status=await send(Buffer.from([1,5]));if(status.phase!==0||status.received!==0)throw Error('firmware did not retain begun session');console.log('PASS: lost begin reply leaves firmware session active and App recovery journal intact without data or retry (offline only)');return;}if(loseChunkReply){if(state.phase!=='recovery-required'||state.confirmedBytes!==100||writes!==2||commits!==0||!saved)throw Error('lost chunk reply did not retain last confirmed offset');const status=await send(Buffer.from([1,5]));if(status.phase!==0||status.received!==200)throw Error('firmware did not retain executed chunk after reply loss');await control('disconnect');await control('expire');await app.verifyRecovery();if(!expiredObserved||app.getSnapshot().phase!=='recovery-required'||!saved||writes!==2||commits!==0)throw Error('volatile expired status incorrectly resolved recovery');console.log('PASS: lost reply, disconnect and expired session preserve App recovery journal without retry (offline only)');return;}if(state.phase!=='recovery-required'||state.confirmedBytes!==bytes.length||writes!==4||commits!==1||!saved)throw Error('app failed to preserve uncertain boot');if(failCommit&&(!rejectedCommit||!rejectedCommit.hasStatus||rejectedCommit.digest))throw Error('failed commit did not produce a rejected status without verified digest');if(loseCommitReply){const status=await send(Buffer.concat([Buffer.from([1,5]),Buffer.from(tx,'hex')]));if(status.phase!==1||status.digest!==hash||status.received!==bytes.length||commits!==1)throw Error('lost reply did not preserve committed firmware status');}console.log(loseCommitReply?'PASS: lost commit reply preserves journal and confirmed firmware status without retry (offline only)':failCommit?'PASS: failed backend commit is rejected without verified digest; App retains recovery journal (offline only)':'PASS: actual App and firmware bridge transferred 336 bytes in four chunks; unverified boot retained recovery journal (offline only)');}finally{child.stdin.end();}}
main().catch(e=>{console.error(e);process.exitCode=1;});
'''

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--app-root', type=Path, required=True)
    p.add_argument('--esbuild', type=Path, required=True)
    args = p.parse_args()
    with tempfile.TemporaryDirectory(prefix='helio-update-seam-') as name:
        temp = Path(name)
        (temp/'firmware.cpp').write_text(CPP)
        subprocess.run(['c++','-std=c++20','-I',str(ROOT/'src'),str(temp/'firmware.cpp'),str(ROOT/'src/core/util/sha256.cpp'),'-o',str(temp/'firmware')],check=True)
        path = args.app_root.resolve()/'apps/mobile/src/firmware/firmwareUpdate.ts'
        (temp/'app.ts').write_text(JS.replace('UPDATE',json.dumps(str(path)),1))
        subprocess.run([str(args.esbuild.resolve()),str(temp/'app.ts'),'--bundle','--platform=node','--format=cjs','--external:expo-crypto','--outfile='+str(temp/'app.cjs')],check=True)
        subprocess.run(['node',str(temp/'app.cjs'),str(temp/'firmware')],check=True,timeout=30)
        subprocess.run(['node',str(temp/'app.cjs'),str(temp/'firmware'),'fail-commit'],check=True,timeout=30)
        subprocess.run(['node',str(temp/'app.cjs'),str(temp/'firmware'),'lose-commit-reply'],check=True,timeout=30)
        subprocess.run(['node',str(temp/'app.cjs'),str(temp/'firmware'),'lose-chunk-reply'],check=True,timeout=30)
        subprocess.run(['node',str(temp/'app.cjs'),str(temp/'firmware'),'lose-begin-reply'],check=True,timeout=30)

if __name__ == '__main__': main()
