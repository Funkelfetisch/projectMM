#pragma once

#include "light/effects/EffectBase.h"

namespace mm {

/// WLED Rainbow Cycle with the Rainbow palette used by the legacy wearable presets.
/// This single-segment scene blanks RGBW outside its selected physical range.
/// @attribution WLED contributors; Rainbow palette by FastLED contributors
class NebuliteRainbowEffect : public EffectBase {
public:
    Dim dimensions() const override { return Dim::D3; }
    bool usesPhysicalLightOrder() const override { return true; }
    uint8_t speed = 128;
    uint8_t size = 128;
    uint16_t segmentStart = 0;
    uint16_t segmentStop = 0;  // exclusive; zero means the end of the strip

    void defineControls() override {
        controls_.addControl("speed", speed, 0, 255);
        controls_.addControl("size", size, 0, 255);
        controls_.addControl("segment start", segmentStart);
        controls_.addControl("segment stop", segmentStop);
    }

    // FastLED 3.6 RainbowColors_p with LINEARBLEND and fixed scale8.
    static RGB rainbowColor(uint8_t index) {
        static constexpr RGB palette[] = {
            {255,0,0}, {213,42,0}, {171,85,0}, {171,127,0},
            {171,171,0}, {86,213,0}, {0,255,0}, {0,213,42},
            {0,171,85}, {0,86,170}, {0,0,255}, {42,0,213},
            {85,0,171}, {127,0,129}, {171,0,85}, {213,0,43},
        };
        const uint8_t hi = index >> 4, lo = index & 15;
        const RGB a = palette[hi];
        if (!lo) return a;
        const RGB b = palette[(hi + 1) & 15];
        const uint8_t second = lo << 4, first = 255 - second;
        const auto fixedScale = [](uint8_t value, uint8_t factor) {
            return static_cast<uint8_t>((uint16_t(value) * (uint16_t(factor) + 1)) >> 8);
        };
        return {uint8_t(fixedScale(a.r, first) + fixedScale(b.r, second)),
                uint8_t(fixedScale(a.g, first) + fixedScale(b.g, second)),
                uint8_t(fixedScale(a.b, first) + fixedScale(b.b, second))};
    }

    static uint8_t wheelIndex(uint32_t now, uint8_t speed, uint8_t size,
                              uint32_t pixel, uint32_t count) {
        if (!count) return 0;
        const uint16_t counter = static_cast<uint16_t>(now * ((speed >> 2) + 2));
        const uint32_t span = 16u << (size / 29);
        return static_cast<uint8_t>((static_cast<uint64_t>(pixel) * span / count) + (counter >> 8));
    }

    void tick() MM_NONBLOCKING override {
        uint8_t* out = buffer();
        const uint32_t count = nrOfLights();
        const uint8_t channels = channelsPerLight();
        if (!out || channels < 3 || !count) return;
        const uint32_t now = elapsed();
        const uint32_t start = segmentStart < count ? segmentStart : count;
        const uint32_t stop = segmentStop == 0 || segmentStop > count ? count : segmentStop;
        for (uint32_t i = 0; i < count; ++i) {
            const RGB c = i >= start && i < stop
                ? rainbowColor(wheelIndex(now, speed, size, i - start, stop - start))
                : RGB{0, 0, 0};
            const size_t offset = static_cast<size_t>(i) * channels;
            out[offset] = c.r; out[offset + 1] = c.g; out[offset + 2] = c.b;
            if (channels > 3) out[offset + 3] = 0;
        }
    }
};
} // namespace mm
