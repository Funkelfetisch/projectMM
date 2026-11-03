#pragma once

#include "light/effects/WledFastNoise.h"
#include "light/effects/WledRandomPalette.h"

namespace mm {
/// Archived WLED-MM Noisefire pixel calculation with FastLED 3.6 fixed scaling.
/// Brightness is the already converted 0..255 audio amplitude; callers must
/// saturate the floating-point microphone value before converting it.
/// The caller supplies the clock and the logical strip size, which may differ
/// from the physical strip on wearable layouts.
/// @attribution WLED contributors; FastLED contributors
class WledNoisefireFrame {
public:
    static RGB pixel(uint32_t position, uint32_t count, uint8_t speed, uint8_t intensity,
                     uint32_t nowMs, uint8_t brightness) {
        if (!count || position >= count) return {0, 0, 0};
        const uint16_t x = uint16_t(position * uint32_t(speed) / 64u);
        // The original strip clock is uint32_t, including multiplication wrap.
        const uint32_t time = (nowMs * uint32_t(speed) / 64u * count) / 255u;
        const uint16_t noise = WledFastNoise::sample(x, uint16_t(time));
        const uint16_t index = uint16_t((255u - position * 256u / count) * noise / (256u - intensity));
        return palette(uint8_t(index), brightness);
    }

    static RGB palette(uint8_t index, uint8_t brightness) {
        static const RGB colors[16] = {
            WledRandomPalette::rainbow({0, 255, 2}), WledRandomPalette::rainbow({0, 255, 4}),
            WledRandomPalette::rainbow({0, 255, 8}), WledRandomPalette::rainbow({0, 255, 8}),
            WledRandomPalette::rainbow({0, 255, 16}), {255, 0, 0}, {255, 0, 0}, {255, 0, 0},
            {255, 140, 0}, {255, 140, 0}, {255, 165, 0}, {255, 165, 0},
            {255, 255, 0}, {255, 165, 0}, {255, 255, 0}, {255, 255, 0}
        };
        RGB a = colors[index >> 4];
        if (const uint8_t lo = index & 15u) {
            const RGB b = colors[((index >> 4) + 1u) & 15u];
            const uint8_t f2 = lo << 4, f1 = 255u - f2;
            a = {uint8_t(scale(a.r, f1) + scale(b.r, f2)),
                 uint8_t(scale(a.g, f1) + scale(b.g, f2)),
                 uint8_t(scale(a.b, f1) + scale(b.b, f2))};
        }
        if (brightness == 255) return a;
        if (!brightness) return {0, 0, 0};
        const uint8_t factor = brightness + 1u;
        return {scale(a.r, factor), scale(a.g, factor), scale(a.b, factor)};
    }
private:
    static uint8_t scale(uint8_t v, uint8_t factor) {
        return uint8_t(uint16_t(v) * (uint16_t(factor) + 1u) >> 8);
    }
};
} // namespace mm
