#pragma once

#include "core/util/color.h"
#include <cstdint>

namespace mm {
/// WLED-MM palette 36, expanded by FastLED 3.6. Snowflakes uses NOBLEND
/// and brightness 64 when spawning a pixel; these are pre-driver RGB values.
/// @attribution WLED, FastLED
struct WledSnowPalette {
    static RGB sample(uint8_t index) {
        static constexpr RGB palette[16] = {
            {0,0,0}, {0,3,15}, {0,6,30}, {0,9,45}, {0,9,45}, {0,18,115},
            {0,28,185}, {0,37,255}, {0,38,255}, {3,100,255}, {3,100,255},
            {23,199,255}, {23,199,255}, {100,235,255}, {100,235,255}, {255,255,255}
        };
        const RGB value = palette[index >> 4];
        // FastLED 3.6 increments nonzero brightness before its fixed scale8.
        const auto scale = [](uint8_t channel) { return static_cast<uint8_t>((uint16_t(channel) * 66u) >> 8); };
        return {scale(value.r), scale(value.g), scale(value.b)};
    }
};
} // namespace mm
