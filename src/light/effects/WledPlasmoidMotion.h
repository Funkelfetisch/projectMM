#pragma once

#include "light/effects/WledWashingMotion.h"
#include <cstdint>

namespace mm {
/// Motion and audio threshold of archived WLED-MM Plasmoid (FX 133).
///
/// Palette, fade, audio acquisition and frame scheduling belong to the caller.
/// @attribution WLED and FastLED contributors
class WledPlasmoidMotion {
public:
    struct Pixel { uint8_t paletteIndex, brightness; };

    /// Step both phases by one frame's beat offset at `now`.
    void advance(uint32_t now) {
        phaseA_ = uint8_t(phaseA_ + beatOffset(6, now));
        phaseB_ = uint8_t(phaseB_ + beatOffset(7, now));
    }
    /// The palette index and brightness of light `i`; brightness is zero while the smoothed volume scaled by `intensity` sits below the index.
    Pixel pixel(uint32_t i, uint8_t speed, uint8_t intensity, float smoothedVolume) const {
        const uint8_t a = uint8_t(i * (1u + 3u * speed / 32u) + phaseA_);
        const uint8_t b = uint8_t(i * (97u + 5u * speed / 32u) + phaseB_);
        const uint8_t index = uint8_t(cubic(a) / 2u + WledWashingMotion::sine(uint8_t(b + 64u)) / 2u);
        return {index, smoothedVolume * intensity / 64.0f < index ? uint8_t{0} : index};
    }
    /// The first of the two beat phases.
    uint8_t phaseA() const { return phaseA_; }
    /// The second of the two beat phases.
    uint8_t phaseB() const { return phaseB_; }
private:
    static uint8_t scale(uint8_t value, uint8_t factor) {
        return uint8_t(uint16_t(value) * (uint16_t(factor) + 1u) >> 8);
    }
    static uint8_t cubic(uint8_t phase) {
        const uint8_t triangle = uint8_t((phase & 128u ? 255u - phase : phase) * 2u);
        const uint8_t square = scale(triangle, triangle), cube = scale(square, triangle);
        const uint16_t value = uint16_t(3u * square - 2u * cube);
        return value & 0x100u ? 255 : uint8_t(value);
    }
    static uint8_t beatOffset(uint8_t bpm, uint32_t now) {
        // FastLED's beat88 uses uint32 arithmetic; WLED's beatsin8_t takes unsigned bounds, so -4 is 252 and range 4-252 wraps to eight.
        const uint32_t product = now * (uint32_t(bpm) << 8) * 280u;
        const uint8_t beat = uint8_t(product >> 24);
        return uint8_t(252u + scale(WledWashingMotion::sine(beat), 8));
    }
    uint8_t phaseA_ = 0, phaseB_ = 0;
};
} // namespace mm
