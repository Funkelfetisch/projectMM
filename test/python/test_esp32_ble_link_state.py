"""Check the native link-counter snapshot; this does not exercise the radio."""
from pathlib import Path
import shutil
import subprocess
import pytest

ROOT = Path(__file__).resolve().parents[2]
CXX = shutil.which('c++') or shutil.which('g++') or shutil.which('clang++')
pytestmark = pytest.mark.skipif(CXX is None, reason='no C++ compiler')


def test_native_link_snapshot_reports_current_peers_and_resets_when_absent(tmp_path):
    source = (ROOT / 'src/platform/esp32/platform_esp32_ble.cpp').read_text()
    start = source.index('bool bleControlLinkState(')
    method = source[start:source.index('\nbool bleControlAllowRepair(', start)]
    seam = r'''
#include "platform/platform.h"
#include <atomic>
#include <cassert>
using namespace mm::platform;
struct Peripheral {
    std::atomic<uint32_t> linkAccepted{7}, linkFailed{2}, linkDisconnected{5};
    std::atomic<int32_t> lastDisconnectReason{8}, lastUpdateStatus{0};
    std::atomic<uint32_t> missingPeerReads{3}, unsupportedAccess{4};
    std::atomic<uint8_t> connectionCount{2};
};
'''
    main = r'''
int main() {
    Peripheral radio; BleControlHandle handle{&radio}; BleLinkState state;
    assert(bleControlLinkState(handle, state));
    assert(state.activeConnections == 2 && state.accepted == 7 && state.failed == 2);
    assert(state.disconnected == 5 && state.lastDisconnectReason == 8 && state.lastUpdateStatus == 0);
    assert(state.missingPeerReads == 3 && state.unsupportedAccess == 4);
    radio.connectionCount.store(1); assert(bleControlLinkState(handle, state) && state.activeConnections == 1);
    handle.impl = nullptr; assert(!bleControlLinkState(handle, state));
    assert(state.activeConnections == 0 && state.lastDisconnectReason == -1 && state.lastUpdateStatus == -1);
}
'''
    cpp, binary = tmp_path / 'link-state.cpp', tmp_path / 'link-state'
    cpp.write_text(seam + '\nnamespace mm::platform {\n' + method + '\n}\n' + main)
    result = subprocess.run([CXX, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'),
                             '-I', str(ROOT / 'src/platform/desktop'), str(cpp), '-o', str(binary)],
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    subprocess.run([str(binary)], check=True, timeout=5)
