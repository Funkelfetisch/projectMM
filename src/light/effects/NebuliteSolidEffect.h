#pragma once

#include "light/effects/EffectBase.h"

namespace mm {

/// A constant RGBW color on one physical strip segment. This preserves the
/// archived wearable looks' segment bounds; the output driver's curve is
/// applied later and is not a claim of WLED gamma equivalence.
class NebuliteSolidEffect : public EffectBase {
public:
    Dim dimensions() const override { return Dim::D3; }
    bool usesPhysicalLightOrder() const override { return true; }
    uint8_t red = 255, green = 0, blue = 0, white = 0, brightness = 255;
    uint16_t segmentStart = 0, segmentStop = 0; // exclusive; zero means the strip end

    void defineControls() override {
        controls_.addControl("red", red, 0, 255);
        controls_.addControl("green", green, 0, 255);
        controls_.addControl("blue", blue, 0, 255);
        controls_.addControl("white", white, 0, 255);
        controls_.addControl("brightness", brightness, 0, 255);
        controls_.addControl("segment start", segmentStart);
        controls_.addControl("segment stop", segmentStop);
    }

    void tick() MM_NONBLOCKING override {
        uint8_t* out = buffer();
        const uint32_t count = nrOfLights();
        const uint8_t channels = channelsPerLight();
        if (!out || channels < 3 || !count) return;
        const uint32_t first = segmentStart < count ? segmentStart : count;
        const uint32_t stop = segmentStop == 0 || segmentStop > count ? count : segmentStop;
        const auto scale = [this](uint8_t value) { return static_cast<uint8_t>((uint16_t(value) * brightness + 127u) / 255u); };
        const uint8_t r = scale(red), g = scale(green), b = scale(blue), w = scale(white);
        for (uint32_t i = 0; i < count; ++i) {
            const bool inside = i >= first && i < stop;
            const size_t offset = static_cast<size_t>(i) * channels;
            out[offset] = inside ? r : 0;
            out[offset + 1] = inside ? g : 0;
            out[offset + 2] = inside ? b : 0;
            if (channels > 3) out[offset + 3] = inside ? w : 0;
        }
    }
};
} // namespace mm
