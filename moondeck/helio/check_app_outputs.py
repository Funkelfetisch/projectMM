#!/usr/bin/env python3
"""Exercise firmware output pages through the real app reader, controller and preview model.

The transport, tree and output hardware are simulated. No phone or BLE link is used.
"""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

from check_ble_outputs import CPP, ROOT

JS = r'''
import {readFileSync} from 'node:fs';
import {parseMoonlightBleResponseHeader} from DECODER;
import {readMoonlightConfiguredOutputs} from READER;
import {MoonlightBleController} from CONTROLLER;
import {configuredPointOutputs} from PREVIEW;
import {ConfiguredOutputsPoller,OUTPUTS_REFRESH_MS} from POLLER;
const lines=readFileSync(process.argv[2],'utf8').trim().split('\n');
const rows=lines.map(line=>{parseMoonlightBleResponseHeader(new TextEncoder().encode(line));return JSON.parse(line)});
const [info,first,second,third,stale,overlap,movedWhite,movedUnknown,single,empty]=rows;
function check(s:any){
 if(s.total!==3||s.drivers.length!==3)throw Error('snapshot');
 const rgb=configuredPointOutputs(s,0,31),white=configuredPointOutputs(s,30,31);
 if(rgb.matches.length!==1||!rgb.rgb||rgb.white||!rgb.unknown)throw Error('rgb point');
 if(white.matches.length!==2||!white.rgb||!white.white||!white.unknown)throw Error('overlap');
}
(async()=>{
 let index=0;const requests:any[]=[];let sequence=[first,second,third];
 const get=async(p:any)=>{requests.push(p);return sequence[index++]};
 check(await readMoonlightConfiguredOutputs(get,()=>true));
 if(JSON.stringify(requests)!==JSON.stringify([{$offset:0},{$offset:1,$rev:first.outputs.revision},{$offset:2,$rev:first.outputs.revision}]))throw Error('cursor');
 index=0;requests.length=0;sequence=[first,stale,first,second,third];
 check(await readMoonlightConfiguredOutputs(get,()=>true));
 if(requests[2].$offset!==0||'$rev' in requests[2])throw Error('stale restart');
 index=0;requests.length=0;sequence=[first,second,third];
 const rpc={request:async(op:string,p:any)=>{if(op!=='outputs')throw Error('op');return get(p)}};
 const controller:any=new MoonlightBleController({id:'test'} as any,{rpc:rpc as any});
 controller.ready=true;controller.identity={extensions:{rpc:2}};controller.info=controller.parseInfo(info);
 if(!controller.supportsConfiguredOutputs)throw Error('capability');
 const original=await controller.readConfiguredOutputs();check(original);
 index=0;sequence=[overlap,movedWhite,movedUnknown];
 const updated=await controller.readConfiguredOutputs();
 const old29=configuredPointOutputs(original,29,31);
 const new29=configuredPointOutputs(updated,29,31),new30=configuredPointOutputs(updated,30,31);
 if(old29.white||!new29.white||new30.white||!new29.rgb||!new30.rgb)throw Error('output refresh role');
 // Run the actual periodic poller over the actual controller and firmware pages.
 // The clock and radio are simulated; the hook and React rendering are not run.
 let timer:any=null, published:any[]=[];const flush=async()=>{for(let i=0;i<100;i++)await Promise.resolve();};
 let live=true;index=0;sequence=[first,second,third];
 const poller=new ConfiguredOutputsPoller({current:()=>live,
  read:current=>controller.readConfiguredOutputs(current,{reuseUnchanged:true}),
  publish:value=>published.push(value),schedule:(fn,ms)=>{if(ms!==OUTPUTS_REFRESH_MS)throw Error('interval');timer=fn;return 1;},cancel:()=>{timer=null;}});
 poller.start();await flush();if(!timer||!published.at(-1)||!configuredPointOutputs(published.at(-1),30,31).white)throw Error('poll initial');
 index=0;sequence=[first];let fire=timer;timer=null;fire();
 if(published.at(-1)!==null)throw Error('role visible during revalidation');
 await flush();if(!timer||index!==1||!configuredPointOutputs(published.at(-1),30,31).white)throw Error('unchanged reuse');
 index=0;sequence=[overlap,movedWhite,movedUnknown];fire=timer;timer=null;fire();await flush();
 if(!timer||index!==3||!configuredPointOutputs(published.at(-1),29,31).white||configuredPointOutputs(published.at(-1),30,31).white)throw Error('automatic output update');
 // Two successive changed-page errors exhaust the reader's one restart.
 // The poller must hide the prior role and later recover without sending controls.
 index=0;sequence=[first,stale,first,stale];fire=timer;timer=null;fire();await flush();
 if(!timer||index!==4||published.at(-1)!==null)throw Error('changed map failure not hidden');
 index=0;sequence=[overlap];fire=timer;timer=null;fire();await flush();
 if(!timer||index!==1||!configuredPointOutputs(published.at(-1),29,31).white)throw Error('role recovery');
 live=false;poller.stop();if(timer!==null)throw Error('poller stopped');
 // Disconnect while the real reader is waiting for its second page. A late
 // reply must neither republish an obsolete role nor schedule another read.
 let release:any, calls=0, latePublished:any[]=[], lateTimer:any=null, connected=true;
 const delayedRpc={request:async(op:string,p:any)=>{
  if(op!=='outputs')throw Error('delayed op');calls++;
  if(calls===1)return first;
  if(calls===2)return await new Promise(resolve=>{release=()=>resolve(second)});
  throw Error('request after disconnect');
 }};
 const delayed:any=new MoonlightBleController({id:'delayed'} as any,{rpc:delayedRpc as any});
 delayed.ready=true;delayed.identity={extensions:{rpc:2}};delayed.info=delayed.parseInfo(info);
 const stopped=new ConfiguredOutputsPoller({current:()=>connected,
  read:current=>delayed.readConfiguredOutputs(current),publish:value=>latePublished.push(value),
  schedule:fn=>{lateTimer=fn;return 1;},cancel:()=>{lateTimer=null;}});
 stopped.start();await flush();if(calls!==2||!release||latePublished.length!==1||latePublished[0]!==null)throw Error('pending page');
 connected=false;stopped.stop();release();await flush();
 if(calls!==2||latePublished.length!==1||lateTimer!==null)throw Error('late disconnected output');
 controller.info=controller.parseInfo({...info,outputsOp:false});requests.length=0;
 if(controller.supportsConfiguredOutputs||await controller.readConfiguredOutputs()!==null||requests.length)throw Error('capability off');
 index=0;sequence=[empty];const no=await readMoonlightConfiguredOutputs(get,()=>true);
 if(no.total!==0||no.drivers.length!==0||!configuredPointOutputs(no,0,31).unknown)throw Error('empty');
 console.log('Firmware -> app controller/reader/preview: periodic role refresh, disconnect during paging, unchanged reuse, overlap, empty and capability gate passed.');
})().catch(err=>{console.error(err);process.exit(1)});
'''

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--app-root',required=True,type=Path)
    p.add_argument('--esbuild',required=True,type=Path)
    p.add_argument('--build-dir',required=True,type=Path)
    args=p.parse_args(); app=args.app_root.resolve(); build=args.build_dir.resolve()
    paths={'DECODER':app/'apps/mobile/src/ble/protocols/moonlightBle.ts',
           'READER':app/'apps/mobile/src/ble/moonlightOutputs.ts',
           'CONTROLLER':app/'apps/mobile/src/ble/controllers/moonlightBle.ts',
           'PREVIEW':app/'apps/mobile/src/preview/configuredGarmentProfile.ts',
           'POLLER':app/'apps/mobile/src/preview/configuredOutputsPoller.ts'}
    models=json.loads((ROOT/'mooninstaller/deviceModels.json').read_text())
    jacket=next(model for model in models if model['name']=='NEBULITE Jacket S3')['state']['Drivers']
    assert jacket['RGB1']['type']=='RmtLedDriver' and jacket['RGB1']['start']==0 and jacket['RGB1'].get('count',0)==0 and jacket['RGB1']['fixture']=='RGB'
    assert jacket['Accent']['type']=='PwmLightDriver' and jacket['Accent']['start']==30 and jacket['Accent']['count']==1 and jacket['Accent']['fixture']=='White'
    code=JS
    for key,path in paths.items(): code=code.replace(key,json.dumps(str(path)),1)
    with tempfile.TemporaryDirectory(prefix='app-outputs-') as tmp:
        tmp=Path(tmp); cpp=tmp/'check.cpp'; exe=tmp/'check'; data=tmp/'replies'; js=tmp/'test.ts'; bundle=tmp/'test.cjs'
        cpp.write_text(CPP); js.write_text(code)
        subprocess.run(['c++','-std=c++20','-I',str(ROOT/'src'),'-I',str(ROOT/'src/platform/desktop'),str(cpp),'-Wl,--start-group',str(build/'libmm_core.a'),str(build/'libmm_platform.a'),'-Wl,--end-group','-ldl','-lpthread','-o',str(exe)],check=True)
        data.write_bytes(subprocess.check_output([str(exe)]))
        subprocess.run([str(args.esbuild.resolve()),str(js),'--bundle','--platform=node','--format=cjs','--external:expo-crypto','--loader:.png=empty','--outfile='+str(bundle)],check=True,capture_output=True)
        subprocess.run(['node',str(bundle),str(data)],check=True)
if __name__=='__main__':main()
