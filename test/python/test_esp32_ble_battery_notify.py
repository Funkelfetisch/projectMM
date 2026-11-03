"""Run the ESP32 battery notification and publication bodies with a recording NimBLE seam.

The seam checks queue/retry and subscription routing, not radio delivery or Android behavior.
"""
from pathlib import Path
import shutil
import subprocess
import pytest

ROOT = Path(__file__).resolve().parents[2]
CXX = shutil.which('c++') or shutil.which('g++') or shutil.which('clang++')
pytestmark = pytest.mark.skipif(CXX is None, reason='no C++ compiler')


def test_notification_failure_retries_latest_valid_levels(tmp_path):
    source = (ROOT / 'src/platform/esp32/platform_esp32_ble.cpp').read_text()
    start = source.index('int accessBattery(uint16_t, uint16_t, ble_gatt_access_ctxt* context, void* arg) {')
    access = source[start:source.index('\nvoid notifyBatteries(', start)]
    start = source.index('void notifyBatteries(ble_npl_event* event) {')
    notify = source[start:source.index('\nbool configureBatteries(', start)]
    start = source.index('bool bleControlPublishBattery(BleControlHandle&')
    publish = source[start:source.index('\n}  // namespace mm::platform', start)]
    seam = r'''
#include "platform/BleBattery.h"
#include "core/util/BlePeerTable.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <mutex>
#include <vector>
using namespace mm;
using namespace mm::platform;
constexpr size_t kPeerCapacity = 2;
struct ble_npl_event { void* arg = nullptr; };
void* ble_npl_event_get_arg(ble_npl_event* e) { return e->arg; }
struct Peripheral;
struct BatterySlot { Peripheral* owner = nullptr; uint8_t physicalSlot = 0; uint16_t valueHandle = 0; std::array<uint8_t, 7> format{}; };
struct BatteryGatt { BleBatteryConfig config{}; BleBatteryUpdates updates{}; BatterySlot slots[2]; ble_npl_event changed; };
struct Peripheral { std::mutex mutex; BatteryGatt* batteries = nullptr; BlePeerTable<2> peers; };
struct BleControlHandle { void* impl = nullptr; };
struct os_mbuf { uint8_t value; };
struct ble_gatt_access_ctxt { int op; os_mbuf* om; };
constexpr int BLE_GATT_ACCESS_OP_READ_DSC = 1, BLE_GATT_ACCESS_OP_READ_CHR = 2;
constexpr int BLE_ATT_ERR_UNLIKELY = 3, BLE_ATT_ERR_INSUFFICIENT_RES = 4;
int os_mbuf_append(os_mbuf* out, const void* data, uint16_t) { out->value = *static_cast<const uint8_t*>(data); return 0; }
struct Sent { uint16_t peer, handle; uint8_t level; };
std::vector<Sent> sent;
bool failAllocation = false, failNotify = false;
int queued = 0;
uint32_t currentTime = 0;
uint32_t millis() { return currentTime; }
void* nimble_port_get_dflt_eventq() { return nullptr; }
void ble_npl_eventq_put(void*, ble_npl_event*) { ++queued; }
os_mbuf* ble_hs_mbuf_from_flat(const void* ptr, int) { return failAllocation ? nullptr : new os_mbuf{*static_cast<const uint8_t*>(ptr)}; }
int ble_gatts_notify_custom(uint16_t peer, uint16_t handle, os_mbuf* buffer) {
    const uint8_t level = buffer->value; delete buffer; // NimBLE consumes it on both success and failure.
    if (failNotify) return 1;
    sent.push_back({peer, handle, level}); return 0;
}
'''
    main = r'''
int main() {
    Peripheral p; BatteryGatt b; p.batteries = &b; b.changed.arg = &p;
    b.config = {2, {1, 2}}; b.slots[0] = {&p, 1, 10}; b.slots[1] = {&p, 2, 20};
    assert(p.peers.connect(3) && p.peers.connect(4));
    assert(p.peers.subscribe(3, 1, true) && p.peers.subscribe(4, 2, true));
    BleControlHandle h{&p}; BleBatterySnapshot value;
    value.slots[0] = {true, true, true, 42}; value.slots[1] = {true, true, true, 75};
    assert(bleControlPublishBattery(h, value)); assert(queued == 1);
    failAllocation = true; notifyBatteries(&b.changed); assert(b.updates.retry && sent.empty());
    failAllocation = false; assert(bleControlPublishBattery(h, value)); assert(queued == 2);
    failNotify = true; notifyBatteries(&b.changed); assert(b.updates.retry && sent.empty());
    value.slots[0].level = 43; value.slots[1].levelValid = false;
    failNotify = false; assert(bleControlPublishBattery(h, value)); assert(queued == 3);
    notifyBatteries(&b.changed); assert(sent.size() == 1);
    assert(sent[0].peer == 3 && sent[0].handle == 10 && sent[0].level == 43);
    assert(!b.updates.retry); assert(bleControlPublishBattery(h, value)); assert(queued == 3);
    os_mbuf result{}; ble_gatt_access_ctxt read{BLE_GATT_ACCESS_OP_READ_CHR, &result};
    assert(accessBattery(3, 10, &read, &b.slots[0]) == 0 && result.value == 43);
    currentTime = BleBatteryUpdates::kPublishTimeoutMs;
    assert(accessBattery(3, 10, &read, &b.slots[0]) == BLE_ATT_ERR_UNLIKELY);
    notifyBatteries(&b.changed); assert(sent.size() == 1);
    assert(bleControlPublishBattery(h, value)); assert(queued == 4);
    notifyBatteries(&b.changed); assert(sent.size() == 2 && sent[1].level == 43);
    // A reused connection handle must not inherit the old peer's BAS subscription.
    assert(p.peers.disconnect(3));
    assert(p.peers.connect(3));
    value.slots[0].level = 44; value.slots[1].levelValid = true;
    assert(bleControlPublishBattery(h, value));
    notifyBatteries(&b.changed);
    assert(sent.size() == 3 && sent[2].peer == 4 && sent[2].handle == 20 && sent[2].level == 75);
    assert(p.peers.subscribe(3, 1, true));
    notifyBatteries(&b.changed);
    assert(sent.size() == 5 && sent[3].peer == 3 && sent[3].handle == 10 && sent[3].level == 44);
    assert(sent[4].peer == 4 && sent[4].handle == 20 && sent[4].level == 75);
    // A conflicting service configuration publishes an empty snapshot. Existing
    // GATT instances remain declared until restart, but neither can return an
    // old level or emit an invented zero while the configuration is invalid.
    assert(bleControlPublishBattery(h, BleBatterySnapshot{}));
    assert(accessBattery(3, 10, &read, &b.slots[0]) == BLE_ATT_ERR_UNLIKELY);
    assert(accessBattery(4, 20, &read, &b.slots[1]) == BLE_ATT_ERR_UNLIKELY);
    notifyBatteries(&b.changed); assert(sent.size() == 5);
    // A corrected configuration may expose only a newly valid reading.
    value.slots[0].level = 55; value.slots[1].levelValid = false;
    assert(bleControlPublishBattery(h, value));
    assert(accessBattery(3, 10, &read, &b.slots[0]) == 0 && result.value == 55);
    assert(accessBattery(4, 20, &read, &b.slots[1]) == BLE_ATT_ERR_UNLIKELY);
    notifyBatteries(&b.changed); assert(sent.size() == 6 && sent[5].level == 55);
}
'''
    cpp, binary = tmp_path / 'battery-notify.cpp', tmp_path / 'battery-notify'
    cpp.write_text(seam + access + notify + publish + main)
    result = subprocess.run([CXX, '-std=c++20', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src'), str(cpp), '-o', str(binary)], capture_output=True, text=True, timeout=30)
    assert result.returncode == 0, result.stdout + result.stderr
    subprocess.run([str(binary)], check=True, timeout=5)
