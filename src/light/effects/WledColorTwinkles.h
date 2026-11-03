#pragma once

#include "light/effects/WledSnowPalette.h"
#include <cstddef>
#include <cstdint>

namespace mm {
/// One archived WLED-MM ColorTwinkles step for a single contiguous segment.
/// The caller owns RGB pixels, one rising flag per pixel and the random stream.
/// @attribution WLED, FastLED
struct WledColorTwinkles {
    template <typename Random>
    static void step(RGB* pixels, uint8_t* rising, size_t count, uint8_t speed,
                     uint8_t intensity, uint8_t brightness, Random& random) {
        if (!pixels || !rising || !count || count > 65535) return;
        const uint8_t up = brightness > 28 ? uint8_t(8u + (speed >> 2)) : uint8_t(68u - brightness);
        const uint8_t down = brightness > 28 ? uint8_t(8u + (speed >> 3)) : uint8_t(68u - brightness);
        for (size_t i = 0; i < count; ++i) {
            RGB& c = pixels[i];
            if (rising[i]) {
                const RGB old = c;
                c = {add(c.r, video(c.r, up)), add(c.g, video(c.g, up)), add(c.b, video(c.b, up))};
                if (c.r == 255 || c.g == 255 || c.b == 255) rising[i] = 0;
                if (c.r == old.r && c.g == old.g && c.b == old.b)
                    c = {add(c.r, c.r), add(c.g, c.g), add(c.b, c.b)};
            } else {
                const uint8_t keep = uint8_t(255u - down);
                c = {scale(c.r, keep), scale(c.g, keep), scale(c.b, keep)};
            }
        }
        for (size_t j = 0; j <= count / 50; ++j) {
            if (random.next8() > intensity) continue;
            for (uint8_t attempt = 0; attempt < 5; ++attempt) {
                const size_t i = random.bounded16(static_cast<uint16_t>(count));
                if (pixels[i].r || pixels[i].g || pixels[i].b) continue;
                pixels[i] = WledSnowPalette::sample(random.next8());
                rising[i] = 1;
                break;
            }
        }
    }
private:
    static uint8_t add(uint8_t a, uint8_t b) { const unsigned n = unsigned(a) + b; return n > 255 ? 255 : uint8_t(n); }
    static uint8_t scale(uint8_t n, uint8_t factor) { return uint8_t((uint16_t(n) * (uint16_t(factor) + 1u)) >> 8); }
    static uint8_t video(uint8_t n, uint8_t factor) { return uint8_t((uint16_t(n) * factor >> 8) + (n && factor ? 1 : 0)); }
};
} // namespace mm
