"""Run the ESP32 BLE rename with a recording NimBLE seam.

The seam checks that a rename sets the GAP name on the host task and restarts a running advertisement to carry it, not radio behavior.
"""
from pathlib import Path
import shutil
import subprocess
import pytest

ROOT = Path(__file__).resolve().parents[2]
CXX = shutil.which('c++') or shutil.which('g++') or shutil.which('clang++')
pytestmark = pytest.mark.skipif(CXX is None, reason='no C++ compiler')

SEAM = r'''
#include "platform/nonblocking.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
struct ble_npl_event { void* arg = nullptr; };
void* ble_npl_event_get_arg(ble_npl_event* e) { return e->arg; }
struct Peripheral { std::mutex mutex; char name[32] = {}; ble_npl_event nameChanged; };
struct BleControlHandle { void* impl = nullptr; };
Peripheral* active = nullptr;
std::string gapName;
bool advertising = false;
int queued = 0, stops = 0, starts = 0, failName = 0;
constexpr int BLE_HS_EALREADY = 2;
const char* kBleTag = "test";
#define ESP_LOGW(...) ((void)0)
void* nimble_port_get_dflt_eventq() { return nullptr; }
void ble_npl_eventq_put(void*, ble_npl_event*) { ++queued; }
int ble_svc_gap_device_name_set(const char* name) { if (failName) return 1; gapName = name; return 0; }
int ble_gap_adv_active() { return advertising; }
int ble_gap_adv_stop() { ++stops; advertising = false; return 0; }
void advertise() { ++starts; advertising = true; }
'''

MAIN = r'''
int main() {
    Peripheral p; p.nameChanged.arg = &p; active = &p;
    BleControlHandle h{&p};
    // The request is taken at once and applied on the host task.
    assert(bleControlSetName(h, "Lisa's Top") && queued == 1 && gapName.empty());
    nameChanged(&p.nameChanged);
    assert(gapName == "Lisa's Top" && stops == 0 && starts == 0);   // not advertising: the next advertisement reads it
    // While advertising, the advertisement restarts so its shortened name follows.
    advertising = true;
    assert(bleControlSetName(h, "Stage-left") && queued == 2);
    nameChanged(&p.nameChanged);
    assert(gapName == "Stage-left" && stops == 1 && starts == 1 && advertising);
    // A name the GAP service refuses leaves the advertisement alone.
    failName = 1;
    assert(bleControlSetName(h, "Refused"));
    nameChanged(&p.nameChanged);
    assert(gapName == "Stage-left" && stops == 1 && starts == 1);
    // Too long, or no transport, is refused before anything is queued.
    assert(!bleControlSetName(h, "a name of thirty-two characters!"));
    BleControlHandle none;
    assert(!bleControlSetName(none, "x") && queued == 3);
}
'''


def test_a_rename_sets_the_gap_name_and_restarts_a_running_advertisement(tmp_path):
    source = (ROOT / 'src/platform/esp32/platform_esp32_ble.cpp').read_text()
    start = source.index('void nameChanged(ble_npl_event* event) {')
    changed = source[start:source.index('\nvoid advertisementChanged(', start)]
    start = source.index('bool bleControlSetName(BleControlHandle& handle, const char* name)')
    setter = source[start:source.index('\nbool bleControlPublishBattery(', start)]
    cpp, binary = tmp_path / 'rename.cpp', tmp_path / 'rename'
    cpp.write_text(SEAM + changed + '\n' + setter + MAIN)
    result = subprocess.run([CXX, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'), str(cpp), '-o', str(binary)],
                            capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    subprocess.run([str(binary)], check=True, timeout=5)
