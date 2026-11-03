#!/usr/bin/env python3
"""Real firmware pages through the real App decoder, reader and controller; offline."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
from check_ble_preview_frame import CPP, ROOT
from check_ble_preview_2d import CPP as CPP_2D

JS = r'''
import {readFileSync} from 'node:fs';
import {parseMoonlightBleResponseHeader} from DECODER;
import {readMoonlightPreviewFrame} from READER;
import {MoonlightBleController} from CONTROLLER;
import {configuredGarmentPoints,configuredGarmentProfile,configuredPointOutputs,createConfiguredGarmentRenderer} from PREVIEW;
import {helioOpticsRgbTextures} from OPTICS;
import {moonlightProductPresentation} from PRODUCT;
const lines=readFileSync(process.argv[2],'utf8').trim().split('\n');
const rows=lines.map(line=>{parseMoonlightBleResponseHeader(new TextEncoder().encode(line));return JSON.parse(line)});
const info=rows[0],pending=rows[1],first=rows[2],second=rows[3],third=rows[4],stale=rows[5],sphere=rows.slice(9,16);
function check(frame:any,total:number){
 if(frame.status!=='ready'||frame.frame.total!==total||frame.frame.rgb.length!==total*3)throw Error('frame dimensions');
 const expected=total===65||total===52?Array.from({length:total},(_,i)=>[Math.min(255,i+10),Math.min(255,2*i+5),3*i]).flat():Array.from({length:222},(_,i)=>[i,255-i,42]).flat();
 if(JSON.stringify(frame.frame.rgb)!==JSON.stringify(expected))throw Error('frame colors');
}
(async()=>{
 let seq:any[]=[pending],i=0;const requests:any[]=[];
 const get=async(p:any)=>{requests.push(p);if(i>=seq.length)throw Error('extra request');return seq[i++];};
 if((await readMoonlightPreviewFrame(get,()=>true)).status!=='pending')throw Error('pending');
 seq=[first,second,third];i=0;requests.length=0;
 check(await readMoonlightPreviewFrame(get,()=>true),65);
 if(requests[1].$offset!==32||requests[1].$rev!==first.frame.revision||requests[2].$offset!==64)throw Error('paging');
 // The second real firmware page can arrive after the controller disconnects.
 // A partial frame must not become a publishable preview on the next session.
 let current=true,releasePage:(value:any)=>void=()=>{throw Error('page not pending')};
 const pendingPage=new Promise(resolve=>{releasePage=resolve});
 const disconnectRequests:any[]=[];
 const interrupted=readMoonlightPreviewFrame(async params=>{
  disconnectRequests.push(params);
  return disconnectRequests.length===1?first:pendingPage;
 },()=>current);
 await new Promise(resolve=>setTimeout(resolve,0));
 if(disconnectRequests.length!==2||disconnectRequests[1].$offset!==32)throw Error('second page not pending');
 current=false;releasePage(second);
 let cancelled=false;
 try{await interrupted}catch(error:any){cancelled=error?.code==='cancelled'}
 if(!cancelled||disconnectRequests.length!==2)throw Error('disconnected preview published or continued');
 seq=[first,stale,...sphere];i=0;requests.length=0;
 check(await readMoonlightPreviewFrame(get,()=>true),222);
 if(requests[2].$offset!==0||'$rev' in requests[2])throw Error('restart');
 seq=sphere;i=0;requests.length=0;
 const rpc={request:async(op:string,p:any)=>{if(op!=='preview-frame')throw Error('op');return get(p)}};
 const controller:any=new MoonlightBleController({id:'test'} as any,{rpc:rpc as any});
 controller.ready=true;controller.identity={extensions:{rpc:2}};controller.info=controller.parseInfo(info);
 if(!controller.supportsPreviewFrame)throw Error('capability');
 const helio=await controller.readPreviewFrame();check(helio,222);
 const texture=helioOpticsRgbTextures(Uint8Array.from(helio.frame.rgb),1536);
 if(texture.count!==222||texture.width!==texture.count*texture.subpixels||!texture.leds.some(v=>v!==0)||texture.wall.length!==222*4)throw Error('HELIO source texture');
 seq=rows.slice(19,21);i=0;requests.length=0;
 const mini=await controller.readPreviewFrame();check(mini,52);
 // Execute the actual App component with React Native and hooks simulated.
 // MINI80 deliberately uses source-order swatches rather than HELIO 1 optics.
 const jsx=(type:any,props:any)=>({type,props});
 const ports:any={react:{},'react/jsx-runtime':{jsx,jsxs:jsx},'react-native':{View:'View',Text:'Text'},
  '../ui/ThemeProvider':{useTheme:()=>({font:{small:{}},colour:{textDim:'#888'},space:()=>0})},
  '../i18n/runtime':{useTranslation:()=>({t:(v:string)=>v})},
  './PreviewViewport':{usePreviewVisibility:()=>({visible:true,ref:{},onLayout(){}})},
  './HelioScenePreview':{HelioScenePreview:'Helio'},'./helioSceneExample':{helioScanExample:()=>null},
  '../ble/moonlightProduct':{moonlightProductPresentation},
  './useControllerPreviewFrame':{useControllerPreviewFrame:()=>({supported:true,phase:'ready',frame:mini.frame})}};
 const component:any={exports:{}};
 new Function('require','module','exports',readFileSync(process.argv[4],'utf8'))((name:string)=>{if(!(name in ports))throw Error('unmocked '+name);return ports[name]},component,component.exports);
 const state:any={reportedProduct:{controllerId:'moonlight:112233445566',model:'HELIO MINI80',ledCount:52,channels:4,configurationProfile:{schema:1,code:2,source:'firmware-target',form:'sphere',verified:false,revision:null,mapping:null,diameterMm:80}}};
 const view=component.exports.ConnectedHelioPreview({controller:{kind:'moonlight-ble',id:'fixture'},state,scope:{},generation:1,currentGeneration:()=>1,busy:false,refreshKey:0});
 const nodes:any[]=[];function walk(n:any){if(Array.isArray(n)){n.forEach(walk);return}if(!n||typeof n!=='object')return;nodes.push(n);walk(n.props?.children)}walk(view);
 const swatches=nodes.filter(n=>n.props?.testID?.startsWith('preview-source-slot-'));
 if(swatches.length!==52||nodes.some(n=>n.type==='Helio'))throw Error('MINI80 wrong visual representation');
 for(let j=0;j<52;j++)if(swatches[j].props.style.backgroundColor!==`rgb(${mini.frame.rgb[j*3]},${mini.frame.rgb[j*3+1]},${mini.frame.rgb[j*3+2]})`)throw Error('MINI80 displayed wrong source color');
 controller.info.previewFrameOp=false;requests.length=0;
 if(await controller.readPreviewFrame()!==null||requests.length)throw Error('capability gate');
 const native=readFileSync(process.argv[3],'utf8').trim().split('\n').map(JSON.parse);
 const [nativePending,nativeFrame,nativeExpected,remapPending,remapFrame,remapExpected]=native;
 for(const row of [nativePending,nativeFrame,remapPending,remapFrame])parseMoonlightBleResponseHeader(new TextEncoder().encode(JSON.stringify(row)));
 for(const [raw,expected] of [[nativeFrame,nativeExpected],[remapFrame,remapExpected]]){
  const got=await readMoonlightPreviewFrame(async()=>raw,()=>true);
  if(got.status!=='ready'||JSON.stringify(got.frame.rgb)!==JSON.stringify(expected.flat()))throw Error('native 2D colors');
 }
 if(nativePending.error!=='frame-pending'||remapPending.error!=='frame-pending'||nativeFrame.frame.revision===remapFrame.frame.revision)throw Error('native invalidation');
 const points=[[5,14],[7,14],[7,14],[0,5],[20,5]],edited=[[20,5],[7,14],[7,14],[0,5],[5,14]];
 const before=configuredGarmentPoints({points,total:5} as any,240),after=configuredGarmentPoints({points:edited,total:5} as any,240);
 if(JSON.stringify(before[0])!==JSON.stringify(after[4])||JSON.stringify(before[4])!==JSON.stringify(after[0])||JSON.stringify(before[1])!==JSON.stringify(before[2]))throw Error('configured point order');
 const garmentController:any={kind:'moonlight-ble',supportsConfiguredPixelMap:true,readConfiguredPixelMap:()=>null};
 const garmentState:any={on:true,reportedProduct:{controllerId:'moonlight:112233445566',model:'NEBULITE Jacket',ledCount:5,channels:3,configurationProfile:{schema:1,code:22,source:'selected-model',form:'garment',verified:false,revision:null,mapping:null,diameterMm:null}}};
 const map:any={total:5,points,revision:'a'.repeat(32)};
 const actual=await readMoonlightPreviewFrame(async()=>nativeFrame,()=>true);
 if(actual.status!=='ready')throw Error('2D frame unavailable');
 let hook=0;const stored={owner:garmentController,generation:1,key:configuredGarmentProfile(garmentState)!.key,map,outputs:null};
 const react:any={useEffect:()=>{},useMemo:(fn:any)=>fn(),useState:(initial:any)=>[hook++===0?stored:initial,()=>{}]};
 const garmentPorts:any={react:{__esModule:true,default:react,...react},'react/jsx-runtime':{jsx,jsxs:jsx},'react-native':{View:'View',Text:'Text'},
  '../ui/ThemeProvider':{useTheme:()=>({font:{small:{}},colour:{text:'#fff',textDim:'#888',textFaint:'#555'},radius:{md:4},space:()=>0})},
  '../i18n/runtime':{useTranslation:()=>({t:(v:string)=>v})},'./PreviewViewport':{usePreviewVisibility:()=>({visible:true,ref:{},onLayout(){}})},
  './Preview':{useFrameClock:()=>0},'./ProductPreview':{ProductPreview:'ProductPreview'},'./overlays':{overlayForKey:()=>({})},
  './configuredGarmentProfile':{configuredGarmentProfile,configuredGarmentPoints,configuredPointOutputs,createConfiguredGarmentRenderer},
  './useControllerConfiguredOutputs':{useControllerConfiguredOutputs:()=>({outputs:null,revision:null})},
  './useControllerPreviewFrame':{useControllerPreviewFrame:()=>({supported:true,frame:actual.frame})}};
 const garmentComponent:any={exports:{}};
 new Function('require','module','exports',readFileSync(process.argv[5],'utf8'))((name:string)=>{if(!(name in garmentPorts))throw Error('unmocked garment '+name);return garmentPorts[name]},garmentComponent,garmentComponent.exports);
 const garmentView=garmentComponent.exports.ConfiguredGarmentPreview({controller:garmentController,state:garmentState,scope:{},generation:1,currentGeneration:()=>1,busy:false,refreshKey:0});
 nodes.length=0;walk(garmentView);const dots=nodes.filter(n=>n.props?.testID==='configured-profile-point');
 if(dots.length!==points.length)throw Error('garment point count');
 for(let j=0;j<points.length;j++)if(dots[j].props.style.left!==before[j].x-4||dots[j].props.style.top!==before[j].y-4||dots[j].props.style.backgroundColor!==`rgb(${actual.frame.rgb[j*3]},${actual.frame.rgb[j*3+1]},${actual.frame.rgb[j*3+2]})`)throw Error('garment native output order');
 console.log('Firmware preview -> App reader/controller and native 2D mapping: paging, restart, colors and remapping passed offline');
})().catch(e=>{console.error(e);process.exitCode=1});
'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--app-root', type=Path, required=True)
    parser.add_argument('--esbuild', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    app = args.app_root.resolve() / 'apps/mobile/src/ble'
    code = JS.replace('DECODER', json.dumps(str(app/'protocols/moonlightBle.ts')), 1).replace('READER', json.dumps(str(app/'moonlightPreviewFrame.ts')), 1).replace('CONTROLLER', json.dumps(str(app/'controllers/moonlightBle.ts')), 1).replace('PREVIEW', json.dumps(str(app.parent/'preview/configuredGarmentProfile.ts')), 1).replace('OPTICS', json.dumps(str(app.parent/'preview/helioOptics.ts')), 1).replace('PRODUCT', json.dumps(str(app/'moonlightProduct.ts')), 1)
    build = args.build_dir.resolve()
    with tempfile.TemporaryDirectory(prefix='preview-app-') as directory:
        tmp = Path(directory); cpp=tmp/'test.cpp'; exe=tmp/'test'; data=tmp/'data.jsonl'; js=tmp/'test.ts'; bundle=tmp/'test.cjs'
        cpp.write_text(CPP); js.write_text(code)
        subprocess.run(['c++','-std=c++20','-I',str(ROOT/'src'),'-I',str(ROOT/'src/platform/desktop'),str(cpp),'-Wl,--start-group',str(build/'libmm_core.a'),str(build/'libmm_platform.a'),'-Wl,--end-group','-ldl','-lpthread','-o',str(exe)],check=True)
        data.write_bytes(subprocess.check_output([str(exe)]))
        cpp.write_text(CPP_2D)
        subprocess.run(['c++','-std=c++20','-I',str(ROOT/'src'),'-I',str(ROOT/'src/platform/desktop'),str(cpp),'-Wl,--start-group',str(build/'libmm_core.a'),str(build/'libmm_platform.a'),'-Wl,--end-group','-ldl','-lpthread','-o',str(exe)],check=True)
        native=tmp/'native.jsonl'; native.write_bytes(subprocess.check_output([str(exe)]))
        subprocess.run([str(args.esbuild.resolve()),str(js),'--bundle','--platform=node','--format=cjs','--external:expo-crypto','--loader:.png=empty','--outfile='+str(bundle)],check=True,capture_output=True)
        component=tmp/'component.cjs'
        subprocess.run([str(args.esbuild.resolve()),str(app.parent/'preview/ConnectedHelioPreview.tsx'),'--platform=node','--format=cjs','--jsx=automatic','--outfile='+str(component)],check=True,capture_output=True)
        garment=tmp/'garment.cjs'
        subprocess.run([str(args.esbuild.resolve()),str(app.parent/'preview/ConfiguredGarmentPreview.tsx'),'--platform=node','--format=cjs','--jsx=automatic','--outfile='+str(garment)],check=True,capture_output=True)
        subprocess.run(['node',str(bundle),str(data),str(native),str(component),str(garment)],check=True)

if __name__ == '__main__': main()
