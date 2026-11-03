#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mm::platform {

/// The declared BAS instances at host startup, using physical slot identities rather than an invented aggregate.
struct BleBatteryConfig {
    uint8_t count = 0;
    std::array<uint8_t, 2> slots{};
    bool operator==(const BleBatteryConfig&) const = default;

    /// One or two unique identities, with unused slots zeroed; zero instances represents no configured battery service.
    bool valid() const {
        if (count > slots.size()) return false;
        for (std::size_t i = 0; i < slots.size(); ++i) {
            if (i >= count) { if (slots[i]) return false; }
            else if (slots[i] < 1 || slots[i] > 2 || (i && slots[i] == slots[0])) return false;
        }
        return true;
    }

    /// GATT CPF: uint8, exponent zero, percentage, SIG namespace, first or second, little-endian (SIG Assigned Numbers).
    static constexpr std::array<uint8_t, 7> presentation(uint8_t slot) {
        return slot >= 1 && slot <= 2 ? std::array<uint8_t, 7>{0x04, 0x00, 0xAD, 0x27, 0x01, slot, 0x00}
                                     : std::array<uint8_t, 7>{};
    }
};

/// A copied measurement for the GATT callback; it never performs ADC or model work.
struct BleBatteryValue {
    bool presenceKnown = false;
    bool present = false;
    bool levelValid = false;
    uint8_t level = 0;
    bool operator==(const BleBatteryValue&) const = default;

    /// BAS defines 0..100 and recommends zero for a known absent battery; unknown or stale values supply no byte.
    bool readLevel(uint8_t& out) const {
        if (!presenceKnown) return false;
        if (!present) { out = 0; return true; }
        if (!levelValid || level > 100) return false;
        out = level;
        return true;
    }
};

/// Fixed physical slots let a configured second-only pack retain its identity.
struct BleBatterySnapshot {
    std::array<BleBatteryValue, 2> slots{};
    bool operator==(const BleBatterySnapshot&) const = default;
};

/// Coalesce changes while retaining failed notifications for the next bounded
/// publisher tick. The caller serializes this state with its subscriber list.
struct BleBatteryUpdates {
    // The service normally publishes every second. Refuse an old snapshot if
    // that task stops, even when the last published value was valid.
    static constexpr uint32_t kPublishTimeoutMs = 5000;
    BleBatterySnapshot values{};
    bool retry = false;
    bool published = false;
    uint32_t publishedAt = 0;
    bool fresh(uint32_t now) const {
        return published && uint32_t(now - publishedAt) < kPublishTimeoutMs;
    }
    bool publish(const BleBatterySnapshot& next, uint32_t now) {
        const bool notify = values != next || retry || !fresh(now);
        values = next;
        published = true;
        publishedAt = now;
        retry = false;
        return notify;
    }
    void deliveryFailed() { retry = true; }
};

} // namespace mm::platform
