#pragma once

#include <cstdint>
#include "core/util/color.h"

namespace mm {

/// WLED palette 3 (primary, primary, secondary, secondary), using the FastLED 16-entry RGB gradient and fixed scale8 interpolation.
///
/// Colors are already corrected by the caller; this helper does not apply gamma.
class WledTwoColorPalette {
public:
    /// The color of `pixel` among `count`, its position mapped to a palette index by `blendMode` and then sampled.
    static RGB sample(RGB primary, RGB secondary, uint32_t pixel, uint32_t count, uint8_t blendMode) {
        uint8_t index = count > 1 ? static_cast<uint8_t>((pixel * 255u) / (count - 1u)) : 0;
        // WLED's solid-palette policy: modes 0 and 2 do not wrap and mode 3 samples without interpolation; the last cell still may blend to 0.
        if (blendMode != 1 && blendMode != 3) index = scale(index, 240);
        return sampleIndex(primary, secondary, index, blendMode == 3);
    }
    /// Sample an already mapped palette index; callers choose wrap before this step.
    static RGB sampleIndex(RGB primary, RGB secondary, uint8_t index, bool discrete) {
        const uint8_t first = index >> 4;
        const uint8_t fraction = index & 15u;
        RGB a = entry(primary, secondary, first);
        if (!fraction || discrete) return a;
        const RGB b = entry(primary, secondary, (first + 1u) & 15u);
        const uint8_t f2 = fraction << 4;
        const uint8_t f1 = 255u - f2;
        return {static_cast<uint8_t>(scale(a.r, f1) + scale(b.r, f2)),
                static_cast<uint8_t>(scale(a.g, f1) + scale(b.g, f2)),
                static_cast<uint8_t>(scale(a.b, f1) + scale(b.b, f2))};
    }
private:
    static uint8_t scale(uint8_t value, uint8_t amount) {
        return static_cast<uint8_t>((uint16_t(value) * (uint16_t(amount) + 1u)) >> 8);
    }
    static uint8_t gradient(uint8_t a, uint8_t b, uint8_t offset) {
        // FastLED uses a signed 8.7 delta divided before doubling.
        const int delta = ((int(b) - int(a)) * 128 / 5) * 2;
        return static_cast<uint8_t>((int(a) * 256 + delta * offset) >> 8);
    }
    static RGB entry(RGB a, RGB b, uint8_t slot) {
        if (slot <= 5) return a;
        if (slot >= 10) return b;
        const uint8_t offset = slot - 5;
        return {gradient(a.r, b.r, offset), gradient(a.g, b.g, offset), gradient(a.b, b.b, offset)};
    }
};
} // namespace mm
