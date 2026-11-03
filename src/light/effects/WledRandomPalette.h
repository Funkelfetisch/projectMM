#pragma once

#include "core/util/color.h"
#include <cstdint>

namespace mm {

/// Palette-1 render primitives from the archived WLED-MM v0.9 and FastLED
/// 3.6. The caller supplies the random CHSV anchors and owns the update clock.
/// @attribution WLED contributors; FastLED contributors
class WledRandomPalette {
public:
    struct HSV { uint8_t h, s, v; };

    /// FastLED's four-anchor HSV shortest-path gradient, then rainbow conversion.
    static void gradient(const HSV (&anchors)[4], RGB (&out)[16]) {
        for (uint8_t part = 0; part < 3; ++part) {
            HSV a = anchors[part], b = anchors[part + 1];
            if (!b.v || !b.s) b.h = a.h;
            if (!a.v || !a.s) a.h = b.h;
            const uint8_t hd = uint8_t(b.h - a.h);
            const int hueDistance = hd > 127 ? -int(uint8_t(0u - hd)) : int(hd);
            const int dh = (hueDistance * 128 / 5) * 2;
            const int ds = ((int(b.s) - int(a.s)) * 128 / 5) * 2;
            const int dv = ((int(b.v) - int(a.v)) * 128 / 5) * 2;
            uint16_t h = uint16_t(a.h) << 8, s = uint16_t(a.s) << 8, v = uint16_t(a.v) << 8;
            for (uint8_t n = 0; n <= 5; ++n) {
                out[part * 5 + n] = rainbow({uint8_t(h >> 8), uint8_t(s >> 8), uint8_t(v >> 8)});
                h = uint16_t(h + dh); s = uint16_t(s + ds); v = uint16_t(v + dv);
            }
        }
    }

    static RGB rainbow(HSV hsv) {
        const uint8_t o = (hsv.h & 31u) << 3;
        const uint8_t third = scale(o, 85), two = scale(o, 170);
        uint8_t r = 0, g = 0, b = 0;
        switch (hsv.h >> 5) {
            case 0: r = 255 - third; g = third; break;
            case 1: r = 171; g = 85 + third; break;
            case 2: r = 171 - two; g = 170 + third; break;
            case 3: g = 255 - third; b = third; break;
            case 4: g = 171 - two; b = 85 + two; break;
            case 5: r = third; b = 255 - third; break;
            case 6: r = 85 + third; b = 171 - third; break;
            default: r = 170 + third; b = 85 - third; break;
        }
        if (hsv.s != 255) {
            if (!hsv.s) r = g = b = 255;
            else {
                uint8_t desat = 255 - hsv.s;
                desat = videoScale(desat, desat);
                const uint8_t satScale = 255 - desat;
                r = uint8_t(scale(r, satScale) + desat);
                g = uint8_t(scale(g, satScale) + desat);
                b = uint8_t(scale(b, satScale) + desat);
            }
        }
        if (hsv.v != 255) {
            const uint8_t value = videoScale(hsv.v, hsv.v);
            r = scale(r, value); g = scale(g, value); b = scale(b, value);
        }
        return {r, g, b};
    }

    /// WLED-MM's 5-second weighted transition, with independent truncation.
    /// The archived profiles change palette every 5 seconds.
    static RGB blend(RGB before, RGB after, uint32_t elapsedMs) {
        if (elapsedMs > 5000) elapsedMs = 5000;
        return {weighted(before.r, after.r, elapsedMs), weighted(before.g, after.g, elapsedMs),
                weighted(before.b, after.b, elapsedMs)};
    }

    /// WLED-MM palette 74 performs up to 128 FastLED 48-channel blend
    /// steps over 250 ms. Each unequal channel rises by one or falls by up
    /// to two per step; after 250 ms the new palette is selected directly.
    static void transition74(const RGB (&before)[16], const RGB (&after)[16],
                             uint32_t elapsedMs, RGB (&out)[16]) {
        if (elapsedMs > 250) { for (unsigned i = 0; i < 16; ++i) out[i] = after[i]; return; }
        const unsigned steps = 128u * elapsedMs / 250u;
        const auto channel = [steps](uint8_t oldValue, uint8_t newValue) -> uint8_t {
            if (oldValue < newValue) {
                const unsigned result = unsigned(oldValue) + steps;
                return static_cast<uint8_t>(result < newValue ? result : newValue);
            }
            const unsigned change = steps * 2u;
            return oldValue > unsigned(newValue) + change ? static_cast<uint8_t>(oldValue - change) : newValue;
        };
        for (unsigned i = 0; i < 16; ++i)
            out[i] = {channel(before[i].r, after[i].r), channel(before[i].g, after[i].g), channel(before[i].b, after[i].b)};
    }

    /// Palette sampling for Washing Machine's solid no-wrap palette mode 0.
    static RGB sample(const RGB (&palette)[16], uint8_t index) {
        index = scale(index, 240);
        const uint8_t first = index >> 4, lo = index & 15;
        const RGB a = palette[first];
        if (!lo) return a;
        const RGB b = palette[(first + 1) & 15];
        const uint8_t second = lo << 4, initial = 255 - second;
        return {uint8_t(scale(a.r, initial) + scale(b.r, second)),
                uint8_t(scale(a.g, initial) + scale(b.g, second)),
                uint8_t(scale(a.b, initial) + scale(b.b, second))};
    }
private:
    static uint8_t weighted(uint8_t a, uint8_t b, uint32_t elapsed) {
        return uint8_t(uint32_t(a) * (5000u - elapsed) / 5000u + uint32_t(b) * elapsed / 5000u);
    }
    static uint8_t scale(uint8_t v, uint8_t factor) {
        return uint8_t((uint16_t(v) * (uint16_t(factor) + 1u)) >> 8);
    }
    static uint8_t videoScale(uint8_t v, uint8_t factor) {
        return uint8_t((uint16_t(v) * factor >> 8) + (v && factor ? 1 : 0));
    }
};
} // namespace mm
