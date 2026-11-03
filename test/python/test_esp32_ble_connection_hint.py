"""Exercise the real connection-hint function against a recording NimBLE seam.

This checks the request and failure behavior, not a central's acceptance or RF.
"""
from pathlib import Path
import shutil
import subprocess
import pytest

ROOT = Path(__file__).resolve().parents[2]
CXX = shutil.which('c++') or shutil.which('g++') or shutil.which('clang++')
pytestmark = pytest.mark.skipif(CXX is None, reason='no C++ compiler')


def test_hint_requests_only_for_short_known_connection(tmp_path):
    source = (ROOT / 'src/platform/esp32/platform_esp32_ble.cpp').read_text()
    start = source.index('void requestConnectionHint(uint16_t handle) {')
    fn = source[start:source.index('\nint gapEvent(', start)]
    seam = r'''
#include "platform/esp32/BleConnectionPolicy.h"
#include <cassert>
#include <cstdint>
using namespace mm::platform;
void fakeLog(unsigned, int) {}
#define ESP_LOGI(tag, fmt, ...) fakeLog(__VA_ARGS__)
struct ble_gap_conn_desc { uint16_t conn_itvl, conn_latency, supervision_timeout; };
struct ble_gap_upd_params { uint16_t itvl_min=0, itvl_max=0, latency=0, supervision_timeout=0, min_ce_len=0, max_ce_len=0; };
ble_gap_conn_desc current{39,0,42}; ble_gap_upd_params requested{};
int lookup=0, callCount=0, updateStatus=0; uint16_t gotHandle=0;
int ble_gap_conn_find(uint16_t, ble_gap_conn_desc* out) { if (lookup) return lookup; *out=current; return 0; }
int ble_gap_update_params(uint16_t handle, const ble_gap_upd_params* p) { gotHandle=handle;requested=*p;++callCount;return updateStatus; }
'''
    main = r'''
int main() {
    requestConnectionHint(7);
    assert(callCount==1 && gotHandle==7 && requested.itvl_min==39 && requested.itvl_max==39);
    assert(requested.latency==0 && requested.supervision_timeout==400);
    lookup=1; requestConnectionHint(8); assert(callCount==1); lookup=0;
    current.supervision_timeout=800; requestConnectionHint(8); assert(callCount==1);
    current={40,3,40}; requestConnectionHint(8); assert(callCount==1);
    current={24,3,100}; updateStatus=1; requestConnectionHint(9);
    assert(callCount==2 && gotHandle==9 && requested.itvl_min==24 && requested.latency==3 && requested.supervision_timeout==400);
}
'''
    cpp, binary = tmp_path / 'hint.cpp', tmp_path / 'hint'
    cpp.write_text(seam + fn + main)
    result = subprocess.run([CXX, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'), str(cpp), '-o', str(binary)], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    subprocess.run([str(binary)], check=True, timeout=5)
