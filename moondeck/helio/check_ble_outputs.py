#!/usr/bin/env python3
"""Exercise actual BLE output pages offline with configured driver windows."""
import json
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CPP = r'''
#include "core/services/BleControlService.h"
#include "helio/BleOutputPage.h"
#include "light/drivers/RmtLedDriver.h"
#include "light/drivers/PwmLightDriver.h"
#include "light/drivers/FixtureProfilesModule.h"
#include <cstdio>
#include <string>
struct Driver : mm::DriverBase {
    void setSourceBuffer(mm::Buffer*) override {}
};
int main() {
    mm::FixtureProfilesModule fixtures; fixtures.setup();
    mm::Scheduler scheduler;
    auto* root = new mm::MoonModule(); root->setName("Drivers"); root->setTypeName("Drivers");
    auto* rgb = new mm::RmtLedDriver(); rgb->setTypeName("RmtLedDriver"); rgb->setName("RGB1"); rgb->setDefaultCorrection("RGB", mm::WhiteMode::None); rgb->rebuildControls(); rgb->rebuildCorrection(255); rgb->setWindow(0, 0); root->addChild(rgb);
    auto* white = new mm::PwmLightDriver(); white->setTypeName("PwmLightDriver"); white->setName("Accent"); white->setDefaultCorrection("White", mm::WhiteMode::None); white->rebuildControls(); white->rebuildCorrection(255); white->setWindow(30, 1); root->addChild(white);
    auto* preview = new Driver(); preview->setTypeName("PreviewDriver"); root->addChild(preview);
    auto* other = new mm::MoonModule(); other->setTypeName("UnknownDriver"); root->addChild(other);
    scheduler.addModule(root);
    mm::BleControlService service; service.setScheduler(&scheduler); service.setOutputReader(&mm::BleOutputPage::writeCurrent);
    service.setEpochForTest("00000000000000000000000000000001");
    service.handle(R"({"$v":1,"$id":1,"$op":"hello","$nonce":"00000000000000000000000000000002"})",100);
    auto call = [&](int id, int offset, const char* revision) {
        char msg[256]; std::snprintf(msg,sizeof(msg),"{\"$v\":1,\"$id\":%d,\"$op\":\"outputs\",\"$e\":\"00000000000000000000000000000001\",\"$s\":1,\"$offset\":%d%s%s%s}",id,offset,revision ? ",\"$rev\":\"" : "",revision ? revision : "",revision ? "\"" : "");
        service.handle(msg, 100 + id); std::puts(service.response());
    };
    service.handle(R"({"$v":1,"$id":2,"$op":"info","$e":"00000000000000000000000000000001","$s":1})",101); std::puts(service.response());
    call(3,0,nullptr);
    std::string response(service.response()); const auto start = response.find("\"revision\":\"") + 12; const auto rev = response.substr(start, 32);
    call(4,1,rev.c_str()); call(5,2,rev.c_str());
    white->setWindow(29,1); call(6,1,rev.c_str());
    call(7,0,nullptr);
    response = service.response(); const auto changedStart = response.find("\"revision\":\"") + 12;
    const auto changedRev = response.substr(changedStart, 32);
    call(8,1,changedRev.c_str()); call(9,2,changedRev.c_str());
    other->setEnabled(false); white->setEnabled(false); call(10,0,nullptr);
    root->setEnabled(false); call(11,0,nullptr);
    call(12,1,nullptr); call(13,1,"bad");
    scheduler.release();
}
'''

def main():
    build = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='ble-outputs-') as tmp:
        source = Path(tmp) / 'check.cpp'; binary = Path(tmp) / 'check'
        source.write_text(CPP)
        subprocess.run(['c++', '-std=c++20', '-I', str(ROOT / 'src'), '-I', str(ROOT / 'src/platform/desktop'), str(source), '-Wl,--start-group', str(build / 'libmm_core.a'), str(build / 'libmm_platform.a'), '-Wl,--end-group', '-ldl', '-lpthread', '-o', str(binary)], check=True)
        raw = subprocess.check_output([str(binary)]).decode().splitlines()
    assert len(raw) == 12 and all(len(line.encode()) <= 512 for line in raw)
    pages = [json.loads(line) for line in raw]
    info, first, second, third, stale, overlap, moved_white, moved_unknown, single, empty, missing_rev, malformed_rev = pages
    assert info['outputsOp'] is True
    assert first['outputs']['total'] == 3 and first['outputs']['next'] == 1
    assert first['outputs']['drivers'] == [{'type':'RmtLedDriver','start':0,'count':0,'rgb':True,'white':False,'name':'RGB1','fixture':'RGB'}]
    assert second['outputs']['drivers'] == [{'type':'PwmLightDriver','start':30,'count':1,'rgb':False,'white':True,'name':'Accent','fixture':'White'}]
    assert third['outputs']['drivers'] == [{'type':'UnknownDriver'}] and third['outputs']['next'] is None
    assert stale['error'] == 'outputs-changed'
    assert overlap['outputs']['revision'] != first['outputs']['revision']
    assert moved_white['outputs']['drivers'][0]['start'] == 29
    assert moved_unknown['outputs']['drivers'] == [{'type':'UnknownDriver'}]
    assert single['outputs']['total'] == 1 and single['outputs']['next'] is None
    assert empty['outputs']['total'] == 0 and empty['outputs']['drivers'] == []
    assert missing_rev['error'] == 'outputs-changed'
    assert malformed_rev['error'] == 'outputs-revision'
    print('BLE output pages: 12 actual dispatcher replies passed (offline)')

if __name__ == '__main__': main()
