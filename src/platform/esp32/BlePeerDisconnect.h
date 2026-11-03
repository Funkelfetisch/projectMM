#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

namespace mm::platform {
/// An unregistered, rejected connection also stopped advertising when it connected.
template <typename Peripheral, typename Advertise>
void blePeerDisconnected(Peripheral& peripheral, uint16_t handle, int reason, Advertise advertise) {
    peripheral.lastDisconnectReason.store(reason, std::memory_order_relaxed);
    peripheral.linkDisconnected.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(peripheral.mutex);
        const bool primary = peripheral.connection.load() == handle;
        if (peripheral.peers.disconnect(handle)) {
            if (primary) {
                peripheral.connection.store(peripheral.peers.firstHandle());
                if (peripheral.peers.size()) peripheral.repeatPairing.cancel();
            }
            peripheral.connectionCount.store(static_cast<uint8_t>(peripheral.peers.size()));
        }
    }
    advertise();
}
} // namespace mm::platform
