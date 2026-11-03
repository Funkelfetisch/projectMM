#pragma once

#include "light/effects/WledWashingMotion.h"
#include <array>
#include <cstdint>

namespace mm {
/// The Twinkleup sine table, built at compile time, and the motion that reads it.
namespace detail {
constexpr std::array<uint8_t, 256> wledSineTable() {
    std::array<uint8_t, 256> table{};
    for (unsigned i = 0; i < table.size(); ++i) table[i] = WledWashingMotion::sine(uint8_t(i));
    return table;
}
inline constexpr auto kWledTwinkleSine = wledSineTable();
}
/// Motion and palette indices of archived WLED-MM Twinkleup (FX 106).
///
/// Palette expansion, RGBW blending and frame timing are owned by the caller.
/// @attribution WLED, FastLED
class WledTwinkleMotion {
public:
    struct Pixel { uint8_t brightness; uint8_t paletteIndex; };
    /// Start a new frame with the original fixed per-frame seed.
    explicit WledTwinkleMotion(uint32_t now, uint8_t speed, uint8_t intensity)
        : now_(now), speed_(speed), intensity_(intensity) {}

    /// The next light's brightness and palette index, advancing the seeded random stream.
    Pixel next() {
        const uint8_t start = random();
        const uint8_t phase = uint8_t(start + uint32_t(16u * now_) / (256u - speed_));
        uint8_t brightness = detail::kWledTwinkleSine[phase];
        if (random() > intensity_) brightness = 0;
        const uint8_t index = uint8_t(random() + now_ / 100u);
        return {brightness, index};
    }

private:
    uint8_t random() {
        seed_ = uint16_t(uint32_t(seed_) * 2053u + 13849u);
        return uint8_t(uint8_t(seed_) + uint8_t(seed_ >> 8));
    }
    uint32_t now_;
    uint16_t seed_ = 535;
    uint8_t speed_, intensity_;
};
} // namespace mm
