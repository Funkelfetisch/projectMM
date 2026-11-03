#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mm {
/// Cooperative, serialized GPIO reservations: a reservation does not by itself arbitrate GPIO APIs or controls that do not consult this table.
template <size_t Capacity>
class PinReservations {
public:
    /// Claim `count` distinct pins for `owner`, all or none; repeating an owner's own claim succeeds, and a pin another owner holds fails.
    bool reserve(const void* owner, const uint16_t* pins, size_t count) {
        if (!owner || !pins || !count || count > Capacity) return false;
        size_t owned = 0, free = 0;
        for (const auto& slot : slots_) {
            if (!slot.owner) ++free;
            else if (slot.owner == owner) ++owned;
        }
        for (size_t i = 0; i < count; ++i) {
            for (size_t j = 0; j < i; ++j) if (pins[i] == pins[j]) return false;
            for (const auto& slot : slots_) if (slot.owner && slot.pin == pins[i] && slot.owner != owner) return false;
        }
        if (owned) {
            if (owned != count) return false;
            for (size_t i = 0; i < count; ++i) if (!heldBy(owner, pins[i])) return false;
            return true;
        }
        if (free < count) return false;
        for (size_t i = 0; i < count; ++i) {
            for (auto& slot : slots_) if (!slot.owner) { slot = {owner, pins[i]}; break; }
        }
        return true;
    }
    /// Free every pin `owner` holds; false when it held none.
    bool release(const void* owner) {
        if (!owner) return false;
        bool found = false;
        for (auto& slot : slots_) if (slot.owner == owner) { slot = {}; found = true; }
        return found;
    }
    /// Whether `owner` holds `pin`.
    bool heldBy(const void* owner, uint16_t pin) const {
        if (!owner) return false;
        for (const auto& slot : slots_) if (slot.owner == owner && slot.pin == pin) return true;
        return false;
    }
    /// Whether any pin is reserved.
    bool any() const { for (const auto& slot : slots_) if (slot.owner) return true; return false; }
    /// Whether any owner holds `pin`.
    bool reserved(uint16_t pin) const {
        for (const auto& slot : slots_) if (slot.owner && slot.pin == pin) return true;
        return false;
    }
    /// Whether an owner other than `owner` holds `pin`.
    bool reservedByOther(const void* owner, uint16_t pin) const {
        for (const auto& slot : slots_) if (slot.owner && slot.pin == pin && slot.owner != owner) return true;
        return false;
    }
private:
    struct Slot { const void* owner = nullptr; uint16_t pin = 0; };
    std::array<Slot, Capacity> slots_{};
};
} // namespace mm
