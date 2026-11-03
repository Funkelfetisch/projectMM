"""Exercise the real BLE service tick with a clock crossing a job deadline.

The fake transport observes publication order; it does not simulate a radio.
"""
from pathlib import Path
import shutil
import subprocess
import pytest

ROOT = Path(__file__).resolve().parents[2]
CXX = shutil.which('c++') or shutil.which('g++') or shutil.which('clang++')
pytestmark = pytest.mark.skipif(CXX is None, reason='no C++ compiler')


def test_deadline_crossing_during_receive_does_not_drop_the_terminal_reply(tmp_path):
    source = (ROOT / 'src/core/services/BleControlService.h').read_text()
    start = source.index('    void tick() MM_NONBLOCKING override {')
    tick = source[start:source.index('\n    /// Resolve the same explicitly configured product hint', start)]
    seam = r'''
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#define MM_NONBLOCKING
struct MoonModule { virtual ~MoonModule() = default; virtual void tick() {} };
struct BleMessage { static constexpr unsigned kCapacity = 512; };
static uint32_t clockNow = 100;
static bool incoming = true;
static bool prepareReady = false;
static std::vector<std::string> published;
namespace platform {
constexpr unsigned kBleControlPeerCapacity = 2;
uint32_t millis() { return clockNow++; }
bool bleControlOwners(int, uint64_t*, size_t, size_t&) { return false; }
bool bleControlReceive(int, char*, size_t, uint64_t& generation) {
    if (!incoming) return false;
    incoming = false; prepareReady = true; generation = 2; return true;
}
bool bleControlPublish(int, const char* text, uint32_t, uint64_t) { published.emplace_back(text); return true; }
}
struct FakePeers { void releaseInactive(uint64_t*, size_t) {} };
struct Service : MoonModule {
    FakePeers* rpc_ = nullptr;
    int transport_ = 0;
    bool dirty = false, terminalDone = false;
    uint64_t replyGeneration_ = 0;
    char response_[40] = {};
    bool flushReply() {
        if (!dirty) return true;
        if (!platform::bleControlPublish(transport_, response_, 0, replyGeneration_)) return false;
        dirty = false; return true;
    }
    void applyDue(uint32_t now) {
        if (!terminalDone && (now >= 101 || prepareReady)) { std::strcpy(response_, "terminal"); dirty = true; terminalDone = true; }
    }
    void handle(const char*, uint32_t now, bool dueAlreadyProcessed = false) {
        if (!dueAlreadyProcessed) applyDue(now);
        std::strcpy(response_, "other-peer"); dirty = true;
    }
'''
    main = r'''
};
int main() {
    Service service;
    service.tick();
    assert(published.size() == 1 && published[0] == "other-peer");
    service.tick();
    assert(published.size() == 2 && published[1] == "terminal");
}
'''
    cpp, binary = tmp_path / 'tick.cpp', tmp_path / 'tick'
    cpp.write_text(seam + tick + main)
    result = subprocess.run([CXX, '-std=c++20', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(binary)],
                            capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    subprocess.run([str(binary)], check=True, timeout=5)
