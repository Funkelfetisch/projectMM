#pragma once

#include "light/effects/EffectBase.h"

namespace mm {

/// A constant RGBW color on one physical strip segment.
///
/// This preserves the archived wearable looks' segment bounds; the output driver's curve is applied later and is not a claim of WLED gamma equivalence.
class NebuliteSolidEffect : public EffectBase {
public:
    /// Spans the whole physical light list, so the effect is volumetric.
    Dim dimensions() const override { return Dim::D3; }
    /// Indexes the real lights in wiring order rather than the layout's bounding box.
    bool usesPhysicalLightOrder() const override { return true; }
    uint8_t red = 255,         ///< The red part of the color.
            green = 0,         ///< The green part of the color.
            blue = 0,          ///< The blue part of the color.
            white = 0,         ///< The white part of the color, shown on RGBW lights.
            brightness = 255;  ///< Scales every part of the color, white included.
    uint16_t segmentStart = 0,   ///< The first light of the segment that shows the color.
             segmentStop = 0;    ///< One past the last light of the segment; zero runs to the end.

    /// Publish the color, brightness and segment controls.
    void defineControls() override {
        controls_.addControl("red", red, 0, 255);
        controls_.addControl("green", green, 0, 255);
        controls_.addControl("blue", blue, 0, 255);
        controls_.addControl("white", white, 0, 255);
        controls_.addControl("brightness", brightness, 0, 255);
        controls_.addControl("segment start", segmentStart);
        controls_.addControl("segment stop", segmentStop);
    }

    /// Fill the segment with the scaled color and blank every light outside it.
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
