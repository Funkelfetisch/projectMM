#pragma once

#include "light/effects/WledPlasmoidMotion.h"
#include "light/effects/WledRandomPalette.h"
#include "light/effects/WledSineMotion.h"

namespace mm {
/// One physical 1D frame of archived WLED-MM Plasmoid. The caller owns the
/// initialized pixels, palette, audio input, clock and frame scheduling.
/// @attribution WLED and FastLED contributors
class WledPlasmoidFrame {
public:
    struct Pixel { uint8_t r, g, b, w; };

    static Pixel add(Pixel a, Pixel b) {
        unsigned r = unsigned(a.r) + b.r, g = unsigned(a.g) + b.g;
        unsigned blue = unsigned(a.b) + b.b, w = unsigned(a.w) + b.w;
        unsigned maximum = r;
        if (g > maximum) maximum = g;
        if (blue > maximum) maximum = blue;
        if (w > maximum) maximum = w;
        if (maximum > 255) {
            r = r * 255 / maximum; g = g * 255 / maximum;
            blue = blue * 255 / maximum; w = w * 255 / maximum;
        }
        return {uint8_t(r), uint8_t(g), uint8_t(blue), uint8_t(w)};
    }

    void step(Pixel* pixels, uint32_t count, const RGB (&palette)[16], Pixel secondary,
              uint32_t now, uint8_t speed, uint8_t intensity, float volume) {
        motion_.advance(now);
        for (uint32_t i = 0; i < count; ++i) {
            // WLED's 1D fade converts to CRGB, losing any prior white channel.
            Pixel faded{scale(pixels[i].r), scale(pixels[i].g), scale(pixels[i].b), 0};
            const auto wave = motion_.pixel(i, speed, intensity, volume);
            const RGB color = WledRandomPalette::sample(palette, wave.paletteIndex);
            Pixel contribution{WledSineMotion::blend(secondary.r, color.r, wave.brightness),
                               WledSineMotion::blend(secondary.g, color.g, wave.brightness),
                               WledSineMotion::blend(secondary.b, color.b, wave.brightness),
                               WledSineMotion::blend(secondary.w, 0, wave.brightness)};
            pixels[i] = add(faded, contribution);
        }
    }
private:
    static uint8_t scale(uint8_t value) { return uint8_t(uint16_t(value) * 208u >> 8); }
    WledPlasmoidMotion motion_;
};
} // namespace mm
