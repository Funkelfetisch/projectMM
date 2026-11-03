#pragma once

#include <cstdint>

namespace mm {
/// Motion primitives of WLED-MM v0.9 Sine (FX 108), using FastLED 3.6 fixed scaling.
/// @attribution WLED contributors; FastLED contributors
struct WledSineMotion {
    static uint32_t advance(uint32_t step, uint8_t speed) { return step + speed / 16u; }
    static uint8_t wave(uint32_t step, uint32_t pixel, uint8_t intensity) {
        const uint8_t phase = uint8_t(pixel * (intensity / 4u) + step);
        const uint8_t triangle = uint8_t((phase & 0x80u ? 255u - phase : phase) * 2u);
        const uint8_t squared = scale(triangle, triangle);
        const uint8_t cubed = scale(squared, triangle);
        const uint16_t value = uint16_t(3u * squared - 2u * cubed);
        return value > 255u ? 255u : uint8_t(value);
    }
    static uint8_t paletteIndex(uint32_t now, uint32_t pixel) {
        const uint16_t colorIndex = uint16_t(now / 32u);
        return uint8_t(pixel * colorIndex / 255u);
    }
    /// The 8-bit channel branch of WLED-MM's RGBW color_blend.
    static uint8_t blend(uint8_t a, uint8_t b, uint8_t amount) {
        if (a == b || amount == 0) return a;
        if (amount == 255) return b;
        return uint8_t(((uint32_t(a) << 8) + b + uint32_t(b) * amount - uint32_t(a) * amount) >> 8);
    }
private:
    static uint8_t scale(uint8_t value, uint8_t factor) {
        return uint8_t((uint16_t(value) * (uint16_t(factor) + 1u)) >> 8);
    }
};
} // namespace mm
