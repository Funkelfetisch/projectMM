#!/usr/bin/env python3
"""Exercise the native 2D renderer through the real BLE preview dispatcher offline."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CPP = r'''
#include "core/services/BleControlService.h"
#include "helio/BlePreviewFramePage.h"
#include "light/drivers/Drivers.h"
#include "light/effects/PlasmaEffect.h"
#include "light/layouts/Layouts.h"
#include "light/layouts/PixelMapLayout.h"
#include "light/layers/Layer.h"
#include <cstdio>
#include <cstring>
int main() {
 mm::Scheduler scheduler;
 mm::Layouts layouts; mm::PixelMapLayout map;
 std::strcpy(map.points,"5,14;7,14;7,14;0,5;20,5"); layouts.addChild(&map);
 mm::Layer layer; layer.setLayouts(&layouts); layer.setChannelsPerLight(3);
 mm::PlasmaEffect plasma; layer.addChild(&plasma); layer.defineControls(); layer.applyState();
 auto* drivers=new mm::Drivers(); drivers->setName("Drivers"); drivers->setTypeName("Drivers");
 auto* preview=new mm::PreviewDriver(); preview->setTypeName("PreviewDriver"); drivers->addChild(preview);
 drivers->multicore=false; drivers->setLayer(&layer); drivers->defineControls(); drivers->applyState(); scheduler.addModule(drivers); drivers->setup();
 mm::BleControlService service; service.setScheduler(&scheduler); service.setPreviewFrameReader(&mm::BlePreviewFramePage::writeCurrent);
 service.setEpochForTest("00000000000000000000000000000001");
 service.handle(R"({"$v":1,"$id":1,"$op":"hello","$nonce":"00000000000000000000000000000002"})",mm::platform::millis());
 const char* request=R"({"$v":1,"$id":2,"$op":"preview-frame","$e":"00000000000000000000000000000001","$s":1,"$offset":0})";
 service.handle(request,mm::platform::millis()); std::puts(service.response());
 layer.tick(); drivers->tick();
 service.handle(R"({"$v":1,"$id":3,"$op":"preview-frame","$e":"00000000000000000000000000000001","$s":1,"$offset":0})",mm::platform::millis()); std::puts(service.response());
 const int coords[][2]={{5,14},{7,14},{7,14},{0,5},{20,5}};
 std::printf("[");
 for(int i=0;i<5;++i) { auto* p=layer.buffer().data()+(coords[i][1]*layer.width()+coords[i][0])*3;
   std::printf("%s[%u,%u,%u]",i?",":"",unsigned(p[0]),unsigned(p[1]),unsigned(p[2])); }
 std::puts("]");
 // Same bounding box and count, but the output order changes. A rebuild must
 // invalidate the old frame even when its dimensions look identical.
 std::strcpy(map.points,"20,5;7,14;7,14;0,5;5,14");
 layer.applyState(); drivers->applyState();
 service.handle(R"({"$v":1,"$id":4,"$op":"preview-frame","$e":"00000000000000000000000000000001","$s":1,"$offset":0})",mm::platform::millis()); std::puts(service.response());
 layer.tick(); drivers->tick();
 service.handle(R"({"$v":1,"$id":5,"$op":"preview-frame","$e":"00000000000000000000000000000001","$s":1,"$offset":0})",mm::platform::millis()); std::puts(service.response());
 const int edited[][2]={{20,5},{7,14},{7,14},{0,5},{5,14}};
 std::printf("[");
 for(int i=0;i<5;++i) { auto* p=layer.buffer().data()+(edited[i][1]*layer.width()+edited[i][0])*3;
   std::printf("%s[%u,%u,%u]",i?",":"",unsigned(p[0]),unsigned(p[1]),unsigned(p[2])); }
 std::puts("]"); scheduler.release();
}
'''

def main():
    build = Path(sys.argv[1])
    with tempfile.TemporaryDirectory(prefix="preview-2d-") as directory:
        source = Path(directory) / "test.cpp"
        exe = Path(directory) / "test"
        source.write_text(CPP)
        subprocess.run(["c++", "-std=c++20", "-I", str(ROOT / "src"), "-I", str(ROOT / "src/platform/desktop"), str(source), "-Wl,--start-group", str(build / "libmm_core.a"), str(build / "libmm_platform.a"), "-Wl,--end-group", "-ldl", "-lpthread", "-o", str(exe)], check=True)
        pending, frame, expected, edit_pending, edited_frame, edited_expected = map(json.loads, subprocess.check_output([str(exe)]).decode().splitlines())
    assert pending["error"] == "frame-pending"
    assert "frame" in frame, frame
    assert frame["frame"]["total"] == 5 and frame["frame"]["next"] is None
    pixels = bytes.fromhex(frame["frame"]["rgb"])
    assert pixels == bytes(channel for rgb in expected for channel in rgb)
    assert expected[1] == expected[2] and len(set(map(tuple, expected))) > 1
    assert edit_pending["error"] == "frame-pending"
    assert edited_frame["frame"]["revision"] != frame["frame"]["revision"]
    assert bytes.fromhex(edited_frame["frame"]["rgb"]) == bytes(channel for rgb in edited_expected for channel in rgb)
    assert edited_expected[1] == edited_expected[2]
    print("Native Plasma 2D -> physical output -> BLE preview, including same-size remapping: passed offline")

if __name__ == "__main__":
    main()
