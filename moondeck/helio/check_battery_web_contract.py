#!/usr/bin/env python3
"""Exercise actual C++ battery telemetry through the real web presentation.

The ADC readings are simulated; this does not qualify a divider, SOC, BLE or hardware.
"""
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CPP = r'''
#include "core/services/BatteryService.h"
#include "core/system/HttpServerModule.h"
#include "platform/platform.h"
#include <cstdio>
static void emit(mm::BatteryChannel& channel, unsigned now) {
    channel.updateText(now);
    mm::JsonSink sink; channel.writeJson(sink, now);
    if (sink.overflowed() || std::strstr(sink.data(), "revision")) std::exit(2);
    for (uint8_t i=0; i<channel.controls().count(); ++i) {
        const auto& c=channel.controls()[i];
        if (std::strcmp(c.name,"sample")==0) { std::puts(static_cast<const char*>(c.ptr)); return; }
    }
    std::exit(7);
}
int main(int argc, char** argv) {
    if (argc != 2) return 9;
    {
        mm::Scheduler scheduler;
        auto* battery = new mm::BatteryService(); battery->setName("Batteries");
        auto* channel = new mm::BatteryChannel(); channel->setName("Pack1");
        channel->setTypeName("BatteryChannel");
        channel->presence=1; channel->gainNumerator=2; channel->gainDenominator=1;
        channel->estimate=true; channel->emptyMv=3000; channel->fullMv=4200;
        battery->addChild(channel); scheduler.addModule(battery); scheduler.setup();
        channel->acceptAdc(1800,1000); channel->updateText(1000);
        mm::HttpServerModule http; http.setScheduler(&scheduler); http.baselineLeafHashesForTest();
        channel->updateText(1000); // same measurement, new telemetry refresh
        mm::JsonSink patch;
        if (!http.buildStatePatchForTest(patch) || patch.overflowed()) return 10;
        auto* output=std::fopen(argv[1],"w"); if (!output) return 11;
        std::fputs(patch.data(),output); std::fclose(output);
        http.release(); scheduler.deleteTree(battery);
    }
    mm::BatteryChannel a, b;
    a.slot=1; b.slot=2;
    for (auto* c : {&a, &b}) {
        c->setTypeName("BatteryChannel");
        c->defineControls();
        c->adcPin=c == &a ? 1 : 3; c->gainNumerator=2; c->gainDenominator=1;
        c->presence=1; c->estimate=true; c->emptyMv=3000; c->fullMv=4200;
        c->staleSeconds=1;
    }
    mm::BatteryService service; service.pollSeconds=1; service.addChild(&a); service.addChild(&b);
    auto drain = [&] {
        for (int i=0; i<500 && service.samplePending(); ++i) {
            mm::platform::delayMs(2); service.tick20ms();
        }
        if (service.samplePending()) std::exit(3);
    };
    mm::platform::setTestNowMs(1000);
    mm::platform::setTestAdcMv(1,1800); mm::platform::setTestAdcMv(3,2100);
    service.applyState(); drain(); emit(a,1000); emit(b,1000);
    auto bas = service.snapshot(1000); uint8_t level = 0;
    if (!bas.slots[0].readLevel(level) || level != 50 ||
        !bas.slots[1].readLevel(level) || level != 100) std::exit(4);
    mm::platform::setTestAdcMv(1,0); mm::platform::setTestAdcMv(3,1900);
    mm::platform::setTestNowMs(2101); service.tick1s(); drain(); emit(a,2101); emit(b,2101);
    bas = service.snapshot(2101); level = 99;
    if (bas.slots[0].readLevel(level) || level != 99 ||
        !bas.slots[1].readLevel(level) || level != 67) std::exit(5);
    mm::platform::setTestAdcMv(1,1950);
    mm::platform::setTestNowMs(3202); service.tick1s(); drain(); emit(a,3202); emit(b,3202);
    bas = service.snapshot(3202);
    if (!bas.slots[0].readLevel(level) || level != 75 ||
        !bas.slots[1].readLevel(level) || level != 67) std::exit(6);
    a.presence=2; a.configureReading(); emit(a,3203);
    b.presence=0; b.configureReading(); b.acceptAdc(1800,3203); emit(b,3204);
    service.release(); mm::platform::clearTestAdcValue(); mm::platform::setTestNowMs(0);
}
'''
JS = r'''
import fs from 'node:fs';
import assert from 'node:assert/strict';
const source=fs.readFileSync(process.argv[2]);
const {batteryView,batteryPresentation,batteryReceiptTracker}=await import('data:text/javascript;base64,'+source.toString('base64'));
const samples=fs.readFileSync(process.argv[3],'utf8').trim().split('\n');
const patch=JSON.parse(fs.readFileSync(process.argv[4],'utf8'));
const samplePatch=patch.patch.find(item=>item.path==='Pack1/sample');
assert.ok(samplePatch, 'battery refresh was not emitted in state patch');
assert.ok(Number.isInteger(JSON.parse(samplePatch.value).revision));
assert.equal(samples.length,8);
const module=(id,value)=>({type:'BatteryChannel',controls:[{name:'slot',value:id},{name:'sample',value}]});
const service=children=>[{type:'BatteryService',children}];
const fresh=batteryView(service([module(1,samples[0]),module(2,samples[1])]));
assert.ok(Number.isInteger(JSON.parse(samples[0]).revision));
assert.notEqual(JSON.parse(samples[0]).revision,JSON.parse(samples[2]).revision);
const tracked=module(1,samples[0]), receipts=batteryReceiptTracker(12000);
const trackedView=now=>batteryView(service([tracked]),{receipts,now}).packs[0];
assert.equal(trackedView(0).percent,50);
assert.equal(trackedView(12001).percent,null);
tracked.controls[1].value=samples[4];
assert.equal(trackedView(13000).percent,75);
assert.deepEqual(fresh.packs.map(p=>p.percent),[50,100]);
assert.deepEqual(fresh.packs.map(p=>batteryPresentation(p).value),['~50%','~100%']);
const stale=batteryView(service([module(1,samples[2]),module(2,samples[3])]));
assert.deepEqual(stale.packs.map(p=>p.percent),[null,67]);
assert.deepEqual(stale.packs.map(p=>batteryPresentation(p).kind),['stale','level']);
const recovered=batteryView(service([module(1,samples[4]),module(2,samples[5])]));
assert.deepEqual(recovered.packs.map(p=>p.percent),[75,67]);
assert.deepEqual(recovered.packs.map(p=>batteryPresentation(p).value),['~75%','~67%']);
const unknown=batteryView(service([module(1,samples[6]),module(2,samples[7])]));
assert.deepEqual(unknown.packs.map(p=>p.percent),[null,null]);
assert.equal(batteryPresentation(unknown.packs[0]).kind,'absent');
assert.equal(batteryPresentation(unknown.packs[1]).kind,'unknown');
const disconnected=batteryView(service([module(1,samples[0]),module(2,samples[1])]),{connected:false});
assert.deepEqual(disconnected.packs.map(p=>p.percent),[null,null]);
console.log('Battery worker, BAS level model and web display: independent fresh, failed and recovered readings pass; web also covers absent, unknown and disconnected.');
'''

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    compiler = shutil.which('c++')
    libraries = [args.build_dir.resolve() / 'libmm_core.a', args.build_dir.resolve() / 'libmm_platform.a']
    if not compiler or not shutil.which('node') or any(not p.is_file() for p in libraries):
        parser.error('C++ compiler, node and desktop libraries required')
    with tempfile.TemporaryDirectory(prefix='battery-web-contract-') as directory:
        temp = Path(directory)
        (temp / 'battery.cpp').write_text(CPP)
        subprocess.run([compiler, '-std=c++20', '-I', str(ROOT / 'src'), '-I', str(ROOT / 'src/platform/desktop'),
                        str(temp / 'battery.cpp'), '-Wl,--start-group', *map(str, libraries), '-Wl,--end-group',
                        '-ldl', '-lpthread', '-o', str(temp / 'battery')], check=True)
        (temp / 'samples.txt').write_bytes(subprocess.check_output([str(temp / 'battery'), str(temp / 'patch.json')]))
        (temp / 'check.mjs').write_text(JS)
        subprocess.run(['node', str(temp / 'check.mjs'), str(ROOT / 'src/ui/helio-battery.js'),
                        str(temp / 'samples.txt'), str(temp / 'patch.json')], check=True)

if __name__ == '__main__':
    main()
