#pragma once

#include "light/effects/WledWaterfallMotion.h"
#include <cstddef>

namespace mm {
/// Stateful 1D Waterfall shift. Callers own color blending, the input frame, the segment's pixel storage and the original peak highlight color.
/// @attribution WLED contributors
class WledWaterfallFrame {
public:
    /// Restart the shift timing from a fresh state.
    void reset() { motion_.reset(); }

    /// Shift the segment by one pixel when the timing allows and write the newest pixel at the end, returning whether it shifted.
    template<class Pixel, class Palette, class Blend>
    bool step(Pixel* pixels, size_t count, uint32_t micros, uint8_t speed,
              uint8_t intensity, bool peak, float frequencyHz, uint8_t magnitude,
              Pixel secondary, Pixel highlight, Palette palette, Blend blend) {
        if (!pixels || !count || !motion_.advance(micros, speed)) return false;
        for (size_t i = 1; i < count; ++i) pixels[i - 1] = pixels[i];
        pixels[count - 1] = peak ? highlight
            : blend(secondary, palette(WledWaterfallMotion::paletteIndex(frequencyHz, intensity)), magnitude);
        return true;
    }
private:
    WledWaterfallMotion motion_;
};
} // namespace mm
