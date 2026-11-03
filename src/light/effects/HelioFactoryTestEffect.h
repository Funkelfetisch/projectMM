#pragma once

#include "light/effects/EffectBase.h"

namespace mm {

/// Operator-selected RGBW channel patterns for a directly mapped ring.
///
/// @guide Match RingLayout and RMT lengths to the physical strip and use this effect alone on its layer.
class HelioFactoryTestEffect : public EffectBase {
public:
    /// Spans the whole physical light list, so the effect is volumetric.
    Dim dimensions() const override { return Dim::D3; }
    /// Indexes the real lights in wiring order rather than the layout's bounding box.
    bool usesPhysicalLightOrder() const override { return true; }
    /// Tags the card with the MoonLight origin.
    const char* tags() const override { return "💫"; }

    enum Phase : uint8_t {
        Off = 0, FullWhite = 1, Chase = 2, Red = 3, Green = 4,
        Blue = 5, White = 6, QuarterWhite = 7,
    };
    /// Selects blackout, channel checks, combined white or a moving pixel.
    uint8_t phase = Off;
    static constexpr const char* kPhases[] = {
        "0 Off / standby", "1 Full RGBW white", "2 Pixel chase 25%",
        "3 Red 100%", "4 Green 100%", "5 Blue 100%",
        "6 White die 100%", "7 RGBW white 25%",
    };

    /// Publish the phase selector.
    void defineControls() override {
        controls_.addSelect("phase", phase, kPhases, 8);
    }

    /// Write the selected RGBW pattern across the buffer, leaving it dark unless the layer is four-channel and unmapped.
    void tick() MM_NONBLOCKING override {
        uint8_t* const data = buffer();
        const size_t count = static_cast<size_t>(nrOfLights());
        const uint8_t cpl = channelsPerLight();
        const size_t bytes = layer()->buffer().bytes();
        if (!data || bytes == 0 || count == 0 || cpl == 0) return;

        // A three-channel or remapped layer fails dark rather than showing a misleading pattern; only the allocated buffer is cleared, never the assumed physical count.
        std::memset(data, 0, bytes);
        if (cpl != 4 || bytes != count * cpl) return;

        const uint8_t selected = phase <= QuarterWhite ? phase : static_cast<uint8_t>(Off);
        const uint32_t now = elapsed();
        if (selected != lastPhase_) {
            lastPhase_ = selected;
            phaseStartMs_ = now;
        }

        // Write every channel on every frame, so no previous scene or phase leaks into a current or channel-order measurement.
        uint8_t r = 0, g = 0, b = 0, w = 0;
        switch (selected) {
            case FullWhite: r = g = b = w = 255; break;
            case Red: r = 255; break;
            case Green: g = 255; break;
            case Blue: b = 255; break;
            case White: w = 255; break;
            case QuarterWhite: r = g = b = w = 64; break;
            case Chase: {
                const size_t index = static_cast<size_t>((now - phaseStartMs_) / 200u) % count;
                uint8_t* const pixel = data + index * cpl;
                pixel[0] = pixel[1] = pixel[2] = pixel[3] = 64;
                return;
            }
            default: break;
        }
        for (size_t i = 0; i < count; ++i) {
            uint8_t* const pixel = data + i * cpl;
            pixel[0] = r; pixel[1] = g; pixel[2] = b; pixel[3] = w;
        }
    }

private:
    uint8_t lastPhase_ = 255;
    uint32_t phaseStartMs_ = 0;
};

} // namespace mm
