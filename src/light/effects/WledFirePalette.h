#pragma once

#include "light/effects/WledRandomPalette.h"

namespace mm {
/// Archived WLED-MM palette 35, expanded by FastLED 3.6 into sixteen slots.
/// The caller supplies the WLED palette index; this samples no-wrap blend mode 0.
/// @attribution WLED, FastLED
struct WledFirePalette {
    static RGB sample(uint8_t index) {
        static constexpr RGB palette[16] = {
            {0,0,0}, {9,0,0}, {18,0,0}, {18,0,0}, {49,0,0}, {81,0,0},
            {112,0,0}, {113,0,0}, {142,3,1}, {175,17,1}, {213,44,2},
            {255,82,4}, {255,115,4}, {255,156,4}, {255,203,4}, {255,255,71}
        };
        return WledRandomPalette::sample(palette, index);
    }
};
} // namespace mm
