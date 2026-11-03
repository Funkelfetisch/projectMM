#pragma once

#include <cstdint>

namespace mm {

/// Timing of the legacy WLED blink effect. The caller supplies WLED's frame
/// time and retains the iteration across frames; rendering and palette
/// interpolation are intentionally separate.
class WledBlinkTiming {
public:
    void reset() { iteration_ = 0; }

    bool next(uint32_t nowMs, uint8_t speed, uint8_t intensity, uint16_t frameMs) {
        // A zero frame time and maximum speed would make the cycle zero.
        if (!frameMs) return false;
        const uint32_t duration = (255u - speed) * 20u;
        const uint32_t onTime = frameMs + ((duration * intensity) >> 8);
        const uint32_t cycle = duration + uint32_t(frameMs) * 2u;
        const uint32_t current = nowMs / cycle;
        const bool on = current != iteration_ || nowMs % cycle <= onTime;
        iteration_ = current;
        return on;
    }

private:
    uint32_t iteration_ = 0;
};
} // namespace mm
