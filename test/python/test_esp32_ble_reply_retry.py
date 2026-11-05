"""Run the ESP32 reply notify loop and publication with a recording NimBLE seam.

The seam checks that a reply the stack cannot take is offered again on a timer, not radio delivery or Android behavior.
"""
from pathlib import Path
import shutil
import subprocess
import pytest

ROOT = Path(__file__).resolve().parents[2]
CXX = shutil.which('c++') or shutil.which('g++') or shutil.which('clang++')
pytestmark = pytest.mark.skipif(CXX is None, reason='no C++ compiler')

SEAM = r'''
#include "core/util/BlePeerTable.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
using namespace mm;
constexpr size_t kPeerCapacity = 2;
struct ble_npl_event { void* arg = nullptr; };
struct ble_npl_callout { ble_npl_event ev; int armed = 0; };
void* ble_npl_event_get_arg(ble_npl_event* e) { return e->arg; }
struct Peripheral { std::mutex mutex; BlePeerTable<kPeerCapacity> peers; uint16_t valueHandle = 7; ble_npl_event changed; ble_npl_callout replyRetry; };
struct BleControlHandle { void* impl = nullptr; };
struct os_mbuf { std::string bytes; };
std::string delivered;
int allocations = 1000, queued = 0;
bool failNotify = false;
uint16_t mtu = 517;
uint32_t ble_npl_time_ms_to_ticks32(uint32_t ms) { return ms; }
int ble_npl_callout_reset(ble_npl_callout* c, uint32_t) { ++c->armed; return 0; }
void* nimble_port_get_dflt_eventq() { return nullptr; }
void ble_npl_eventq_put(void*, ble_npl_event*) { ++queued; }
uint16_t ble_att_mtu(uint16_t) { return mtu; }
os_mbuf* ble_hs_mbuf_from_flat(const void* p, uint16_t n) {
    if (allocations-- <= 0) return nullptr;
    return new os_mbuf{std::string(static_cast<const char*>(p), n)};
}
int ble_gatts_notify_custom(uint16_t, uint16_t handle, os_mbuf* buffer) {
    const std::string bytes = buffer->bytes; delete buffer;   // NimBLE consumes it on both success and failure.
    assert(handle == 7);
    if (failNotify) return 6;
    delivered += bytes; return 0;
}
'''

MAIN = r'''
int main() {
    Peripheral p; p.changed.arg = &p; p.replyRetry.ev.arg = &p;
    assert(p.peers.connect(3) && p.peers.subscribe(3, 0, true));
    uint64_t generation = 0; assert(p.peers.generations(&generation, 1) == 1);
    BleControlHandle h{&p};
    const std::string last(300, 'z');

    // The last page of a stream meets a stack out of buffers: nothing later is published, so the timer alone resumes it.
    assert(bleControlPublish(h, last.c_str(), generation) && queued == 1);
    allocations = 0; sendReplies(&p.changed);
    assert(delivered.empty() && p.replyRetry.armed == 1);
    allocations = 1000; sendReplies(&p.replyRetry.ev);
    assert(delivered == last + "\n" && p.replyRetry.armed == 1);

    // A notify the stack refuses is offered again the same way.
    delivered.clear();
    assert(bleControlPublish(h, "{\"id\":2}", generation) && queued == 2);
    failNotify = true; sendReplies(&p.changed);
    assert(delivered.empty() && p.replyRetry.armed == 2);
    failNotify = false; sendReplies(&p.replyRetry.ev);
    assert(delivered == "{\"id\":2}\n");

    // A reply cut off mid-way resumes where it stopped, and the next one waits for it: the caller retries, no event is queued.
    delivered.clear(); mtu = 23;
    const std::string page(100, 'p');
    assert(bleControlPublish(h, page.c_str(), generation) && queued == 3);
    allocations = 2; sendReplies(&p.changed);
    assert(delivered == page.substr(0, 40) && p.replyRetry.armed == 3);
    assert(!bleControlPublish(h, "{\"id\":4}", generation) && queued == 3);
    allocations = 1000; sendReplies(&p.replyRetry.ev);
    assert(delivered == page + "\n" && p.replyRetry.armed == 3);
    assert(bleControlPublish(h, "{\"id\":4}", generation) && queued == 4);
    sendReplies(&p.changed);
    assert(delivered == page + "\n{\"id\":4}\n");
}
'''


def test_a_reply_the_stack_cannot_take_is_offered_again_on_a_timer(tmp_path):
    source = (ROOT / 'src/platform/esp32/platform_esp32_ble.cpp').read_text()
    start = source.index('constexpr uint32_t kReplyRetryMs')
    send = source[start:source.index('\nvoid notifyUpdateAck(', start)]
    start = source.index('bool bleControlPublish(BleControlHandle& handle, const char* response')
    publish = source[start:source.index('\nbool bleControlPublishAdvertisement(', start)]
    cpp, binary = tmp_path / 'reply-retry.cpp', tmp_path / 'reply-retry'
    cpp.write_text(SEAM + send + '\n' + publish + MAIN)
    result = subprocess.run([CXX, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'), str(cpp), '-o', str(binary)],
                            capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    subprocess.run([str(binary)], check=True, timeout=5)
