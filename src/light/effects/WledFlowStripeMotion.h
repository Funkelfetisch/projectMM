#pragma once

#include "light/effects/WledWashingMotion.h"
#include <cstdint>

namespace mm {
/// Hue calculation from archived WLED-MM Flow Stripe (FX 179).
/// @attribution WLED contributors, ldirko, Andrew Tuline
struct WledFlowStripeMotion {
    /// Writes the hue of `pixel` to `out`, false for a degenerate range: the original divides by floor(count*10/13), which is zero for one pixel.
    static bool hue(uint32_t now, uint8_t speed, uint8_t intensity,
                    uint16_t pixel, uint16_t count, uint8_t& out) {
        const uint16_t half = static_cast<uint16_t>(uint32_t(count) * 10u / 13u);
        if (!half || pixel >= count) return false;
        const uint8_t baseHue = static_cast<uint8_t>(now / (uint32_t(speed) + 1u));
        const uint32_t time = now / (uint32_t(intensity) / 8u + 1u);
        const uint16_t distance = pixel > half ? pixel - half : half - pixel;
        const uint8_t c = WledWashingMotion::sine(static_cast<uint8_t>((distance / half) * 127u));
        const uint8_t wave = WledWashingMotion::sine(static_cast<uint8_t>(c / 2u + time));
        const uint8_t brightness = WledWashingMotion::sine(static_cast<uint8_t>(wave + time / 8u));
        out = static_cast<uint8_t>(brightness + baseHue);
        return true;
    }
};
} // namespace mm
