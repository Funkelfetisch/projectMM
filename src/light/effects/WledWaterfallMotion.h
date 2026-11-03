#pragma once

#include <cmath>
#include <cstdint>

namespace mm {
/// Timing and color-index calculation for the archived 1D WLED Waterfall.
///
/// The caller owns pixels, palette, audio, and a wrapping 32-bit microsecond clock.
/// @attribution WLED contributors
class WledWaterfallMotion {
public:
    /// Forget the last beat so the next call advances.
    void reset() { previous_ = 255; }

    /// Whether `micros` has reached a new beat at `speed`, so the effect advances a frame; speed 255 always advances.
    bool advance(uint32_t micros, uint8_t speed) {
        // The original expression is `... + 1 % 16`: % binds before +.
        const uint8_t hand = uint8_t(micros / (256u - speed) / 500u + 1u);
        if (speed != 255 && hand == previous_) return false;
        previous_ = hand;
        return true;
    }

    /// The palette index for a frequency in Hz, offset by `intensity`; an invalid frequency or one below 182 Hz indexes from zero.
    static uint8_t paletteIndex(float frequencyHz, uint8_t intensity) {
        // Avoid the original's out-of-range float-to-byte conversion for low or invalid frequencies, while keeping its explicit low gate.
        uint8_t color = 0;
        if (std::isfinite(frequencyHz) && frequencyHz >= 182.0f) {
            const float scaled = (std::log10(frequencyHz) - 2.26f) * 150.0f;
            // Audio outside the original byte conversion's defined domain is bounded; physical frequency calibration remains separate.
            if (scaled > 0) color = scaled >= 255 ? 255 : uint8_t(scaled);
        }
        return uint8_t(color + intensity);
    }
private:
    uint8_t previous_ = 255;
};
} // namespace mm
