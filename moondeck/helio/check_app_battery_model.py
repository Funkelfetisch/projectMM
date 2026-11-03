#!/usr/bin/env python3
"""Compare actual firmware battery RPC replies with an app checkout, offline.

The scheduler and receipt clock are simulated; this does not test BLE,
ADC accuracy, Android BAS presentation or actual battery hardware.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CPP = r'''
#include "core/services/BleControlService.h"
#include "core/services/BatteryService.h"
#include <cstdio>
int main(int argc, char** argv) {
 struct Pins final : mm::MoonModule {
   int8_t gpio = 2;
   void defineControls() override { controls_.addPin("gpio", gpio); }
 };
 mm::Scheduler scheduler; mm::BleControlService service; service.setScheduler(&scheduler);
 service.setEpochForTest("00000000000000000000000000000001");
 service.handle(R"({"$v":1,"$id":1,"$op":"hello","$nonce":"00000000000000000000000000000002"})",0);
 service.handle(R"({"$v":1,"$id":2,"$op":"info","$e":"00000000000000000000000000000001","$s":1})",0);
 std::puts(service.response());
 unsigned id = 6;
 auto emit = [&](unsigned now) {
   mm::platform::setTestNowMs(now);
   char request[160]; std::snprintf(request,sizeof(request),"{\"$v\":1,\"$id\":%u,\"$op\":\"battery\",\"$e\":\"00000000000000000000000000000001\",\"$s\":1}",id++);
   service.handle(request,now);
   std::puts(service.response());
 };
 emit(0); // no battery service
 auto* battery = new mm::BatteryService(); battery->setName("Battery"); battery->setTypeName("BatteryService"); scheduler.addModule(battery);
 auto* a = new mm::BatteryChannel(); auto* b = new mm::BatteryChannel();
 a->setTypeName("BatteryChannel"); b->setTypeName("BatteryChannel"); a->slot=1; b->slot=2;
 battery->addChild(a); battery->addChild(b);
 for (auto* c : {a,b}) { c->gainNumerator=2; c->gainDenominator=1; c->emptyMv=3300; c->fullMv=4200; c->estimate=true; c->staleSeconds=1; }
 a->presence=0; b->presence=2; a->prepare(); b->prepare(); emit(0);
 a->presence=1; b->presence=1; a->configureReading(); b->configureReading();
 a->acceptAdc(1650,100); b->acceptAdc(2100,100); emit(100); emit(1101);
 b->acceptAdc(1950,1101); emit(1101);
 a->adcPin=15; b->adcPin=15; emit(1101); // duplicate ADC: explicit configuration error
 a->adcPin=1; a->senseGate=2; a->settleMs=10; b->adcPin=-1;
 auto* pins = new Pins(); pins->setName("Other"); scheduler.addModule(pins);
 scheduler.setup(); // the other module declares the battery gate pin
 a->acceptAdc(1800,1101); scheduler.prepareSubtree(battery);
 battery->tick1s(); emit(1101); // real pin conflict and invalidated prior sample
 if (argc != 2) return 2;
 auto* web = std::fopen(argv[1], "w"); if (!web) return 3;
 auto emitWeb = [&](uint32_t now) {
   const char* configuration = nullptr;
   for (uint8_t i=0; i<battery->controls().count(); ++i) {
     const auto& c=battery->controls()[i];
     if (std::strcmp(c.name,"configuration")==0) configuration=static_cast<const char*>(c.ptr);
   }
   if (!configuration) std::exit(4);
   mm::JsonSink first, second; a->updateText(now); b->updateText(now);
   a->writeJson(first,now,true); b->writeJson(second,now,true);
   std::fprintf(web,"{\"configuration\":\"%s\",\"samples\":[%s,%s]}\n",configuration,first.data(),second.data());
 };
 emitWeb(1101);
 if (scheduler.setControl("Other","gpio",R"({"value":5})") != mm::Scheduler::SetControlResult::Ok) return 5;
 a->settleMs=200; scheduler.prepareSubtree(battery); emitWeb(1101);
 // Current independent samples followed by a simulated gate-off failure.
 a->acceptAdc(1800,1101); b->acceptAdc(1950,1101);
 for (int i=0; i<500 && !mm::platform::gpioRead(2); ++i) mm::platform::delayMs(2);
 if (!mm::platform::gpioRead(2)) return 6;
 mm::platform::GpioCapability denied; denied.reserved=true; mm::platform::setTestGpioCapability(2,denied);
 for (int i=0; i<500 && a->reading().fresh(1101); ++i) { mm::platform::delayMs(2); battery->tick20ms(); }
 if (a->reading().fresh(1101) || !b->reading().fresh(1101)) return 7;
 auto bas=battery->snapshot(1101); uint8_t level=0;
 if (bas.slots[0].readLevel(level) || !bas.slots[1].readLevel(level) || level!=67) return 8;
 emit(1101); mm::platform::clearTestGpioCapability();
 // Both dividers share the same gate: either channel's gate-off fault must
 // invalidate both previous values before publishing BLE/BAS data.
 b->adcPin=3; b->senseGate=2; b->settleMs=200;
 mm::platform::setTestAdcMv(1,1800); mm::platform::setTestAdcMv(3,1950);
 scheduler.prepareSubtree(battery);
 a->acceptAdc(1800,1101); b->acceptAdc(1950,1101);
 for (int i=0; i<500 && !mm::platform::gpioRead(2); ++i) mm::platform::delayMs(2);
 if (!mm::platform::gpioRead(2)) return 9;
 mm::platform::setTestGpioCapability(2,denied);
 for (int i=0; i<500 && a->reading().fresh(1101); ++i) { mm::platform::delayMs(2); battery->tick20ms(); }
 if (a->reading().fresh(1101) || b->reading().fresh(1101)) return 10;
 bas=battery->snapshot(1101);
 if (bas.slots[0].readLevel(level) || bas.slots[1].readLevel(level)) return 11;
 emit(1101); mm::platform::clearTestGpioCapability();
 scheduler.prepareSubtree(battery);
 for (int i=0; i<1000 && battery->samplePending(); ++i) { mm::platform::delayMs(2); battery->tick20ms(); }
 if (battery->samplePending() || !a->reading().fresh(1101) || !b->reading().fresh(1101)) return 12;
 emit(1101);
 a->acceptAdc(1800,1101); b->acceptAdc(1950,1101);
 emitWeb(1101); b->acceptAdc(1950,2102); emitWeb(2102); emitWeb(3103);
 auto* duplicate = new mm::BatteryService(); duplicate->setName("Battery2"); duplicate->setTypeName("BatteryService"); scheduler.addModule(duplicate);
 emit(3103); // conflicting services must never select a potentially wrong pack
 duplicate->setEnabled(false); emit(3103); // corrected configuration becomes readable again
 a->acceptAdc(1800,3103); b->acceptAdc(1950,3103); emit(3103); emitWeb(3103);
 a->gainNumerator=3; a->configureReading(); emit(3103); emitWeb(3103); // old voltage must not survive a changed divider
 a->acceptAdc(1250,3103); emit(3103); emitWeb(3103); // only a new ADC sample can restore the first estimate
 std::fclose(web);
 scheduler.release(); mm::platform::setTestNowMs(0);
}
'''
JS = r'''
import {MoonlightBleController} from CONTROLLER;
import {presentBatteryTelemetry} from VIEW;
import {batteryView,batteryPresentation} from WEB;
import {readFileSync} from 'node:fs';
const rows=readFileSync(process.argv[2],'utf8').trim().split('\n');
const expected=[[],['unknown',null,'absent',null],['available',0,'available',100],['stale',null,'stale',null],['stale',null,'available',67],[],[],['unknown',null,'available',67],['unknown',null,'unknown',null],['available',33,'available',67],[],['stale',null,'stale',null],['available',33,'available',67],['unknown',null,'available',67],['available',50,'available',67]];
if(rows.length!==expected.length+1) throw Error('fixture count');
const info=JSON.parse(rows.shift()!); let current:any;
const rpc={request:async(op:string)=>{if(op!=='battery')throw Error('unexpected operation');return current;},isConnected:async()=>true};
const controller:any=new MoonlightBleController({id:'test'} as any,{rpc:rpc as any,now:()=>2000});
controller.identity={batteryOp:true}; controller.info=controller.parseInfo(info);
(async()=>{for(let i=0;i<rows.length;i++) {
 current=JSON.parse(rows[i]); controller.battery=null;
 await controller.readBattery(0,true);
 const telemetry=controller.battery; if(!telemetry)throw Error(`App rejected firmware battery reply ${i}`);
 const p=presentBatteryTelemetry(telemetry,{connected:true,nowMs:2000,receiptStaleAfterMs:1000});
 const got=p.length?[p[0]?.status,p[0]?.percent,p[1]?.status,p[1]?.percent]:[];
 if(JSON.stringify(got)!==JSON.stringify(expected[i]))throw Error(`case ${i}: ${JSON.stringify(got)}`);
 if((telemetry.error??null)!==((i===5||i===6||i===10)?'invalid-configuration':null))throw Error(`case ${i}: configuration error`);
 const disconnected=presentBatteryTelemetry(telemetry,{connected:false,nowMs:2000,receiptStaleAfterMs:1000});
 if(disconnected.some(pack=>pack.percent!==null))throw Error('disconnected percentage');
}
console.log(`Firmware RPC replies decoded and presented: ${rows.length} cases; independent packs; disconnect clears displayed percentages`);
const web=readFileSync(process.argv[3],'utf8').trim().split('\n').map(JSON.parse);
const view=(entry:any)=>batteryView([{type:'BatteryService',controls:[{name:'configuration',value:entry.configuration}],children:entry.samples.map((sample:any)=>({type:'BatteryChannel',controls:[{name:'slot',value:sample.id},{name:'sample',value:JSON.stringify(sample)}]}))}]);
const failed=view(web[0]), recovered=view(web[1]);
if(failed.error!=='invalid-configuration' || failed.packs[0].percent!==null || !batteryPresentation(failed.packs[0]).detail.includes('configuration error'))throw Error('web conflict presentation');
if(recovered.error==='invalid-configuration')throw Error('web configuration did not recover');
const fresh=view(web[2]), partial=view(web[3]), stale=view(web[4]);
if(JSON.stringify(fresh.packs.map((p:any)=>p.percent))!=='[33,67]')throw Error('web independent fresh values');
if(JSON.stringify(partial.packs.map((p:any)=>p.percent))!=='[null,67]')throw Error('web independent stale values');
if(stale.packs.some((p:any)=>p.percent!==null))throw Error('web exposed stale value');
if(JSON.stringify(web.slice(5,8).map((row:any)=>view(row).packs.map((p:any)=>p.percent)))!=='[[33,67],[null,67],[50,67]]')throw Error('web retained a reading across divider calibration');
console.log('Web displays two real firmware samples independently and hides them after expiry; pin conflict clears after correction');
})().catch(error=>{console.error(error);process.exitCode=1;});
'''


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--app-root', type=Path, required=True)
    p.add_argument('--esbuild', type=Path, required=True)
    p.add_argument('--build-dir', type=Path, default=ROOT/'build/gcc-nebulite-s3')
    args = p.parse_args()
    app = args.app_root.resolve() / 'apps/mobile/src/ble'
    compiler = shutil.which('c++')
    if not compiler or not shutil.which('node'):
        p.error('C++ compiler and node are required')
    import json
    with tempfile.TemporaryDirectory(prefix='moonlight-battery-compat-') as name:
        tmp = Path(name)
        (tmp/'sample.cpp').write_text(CPP)
        libs = [args.build_dir.resolve()/'libmm_core.a', args.build_dir.resolve()/'libmm_platform.a']
        subprocess.run([compiler, '-std=c++20', '-I', str(ROOT/'src'), '-I', str(ROOT/'src/platform/desktop'), str(tmp/'sample.cpp'), '-Wl,--start-group', *map(str, libs), '-Wl,--end-group', '-ldl', '-lpthread', '-o', str(tmp/'sample')], check=True)
        samples = subprocess.check_output([str(tmp/'sample'), str(tmp/'web.jsonl')])
        rows = samples.splitlines()
        if len(rows) != 16 or any(len(row) > 512 for row in rows):
            raise RuntimeError('invalid firmware reply count or response exceeds 512 bytes')
        actual = [json.loads(row) for row in rows]
        if actual[0].get('op') != 'info' or any(not row.get('ok') or row.get('op') != 'battery' or row.get('id') != 6 + i for i, row in enumerate(actual[1:])):
            raise RuntimeError('firmware battery RPC failed')
        if actual[7]['battery'].get('error') != 'invalid-configuration':
            raise RuntimeError(f"fixture did not produce the expected configuration error: {actual[7]}")
        if any(pack.get('percent') is not None for pack in actual[7]['battery']['packs']):
            raise RuntimeError('pin conflict retained an old percentage')
        (tmp/'samples.jsonl').write_bytes(samples)
        js = JS.replace('CONTROLLER', json.dumps(str(app/'controllers/moonlightBle.ts')), 1).replace('VIEW', json.dumps(str(app/'batteryTelemetry.ts')), 1).replace('WEB', json.dumps(str(ROOT/'src/ui/helio-battery.js')), 1)
        (tmp/'check.ts').write_text(js)
        subprocess.run([str(args.esbuild.resolve()), str(tmp/'check.ts'), '--bundle', '--platform=node', '--format=cjs', '--external:expo-crypto', '--outfile='+str(tmp/'check.cjs')], check=True)
        subprocess.run(['node', str(tmp/'check.cjs'), str(tmp/'samples.jsonl'), str(tmp/'web.jsonl')], check=True)


if __name__ == '__main__':
    main()
