#pragma once
#include <cstdint>

namespace mm::platform {
enum class BleAdvertisementAction : uint8_t { Wait, Start, Restart };

/// Decide whether to refresh the legacy advertisement without displacing peers.
inline BleAdvertisementAction bleAdvertisementAction(bool slotAvailable, bool advertising,
                                                      bool current, bool updateKnown,
                                                      uint32_t now, uint32_t updatedAt) {
    if (!slotAvailable) return BleAdvertisementAction::Wait;
    if (!advertising) return BleAdvertisementAction::Start;
    if (current || (updateKnown && now - updatedAt < 1000u)) return BleAdvertisementAction::Wait;
    return BleAdvertisementAction::Restart;
}
} // namespace mm::platform
