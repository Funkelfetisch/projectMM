#pragma once

#include "light/effects/EffectBase.h"
#include "light/effects/WledTwoColorPalette.h"

namespace mm {
/// Archived WLED-MM Palette (FX 65), palette 3, moving-wrap mode 0.
/// RGB output is uncorrected; WLED gamma, transitions and physical output need qualification.
class NebulitePaletteEffect : public EffectBase {
public:
    Dim dimensions() const override { return Dim::D3; }
    bool usesPhysicalLightOrder() const override { return true; }
    uint8_t speed = 128;
    uint8_t red = 0, green = 255, blue = 200;
    uint8_t secondaryRed = 255, secondaryGreen = 0, secondaryBlue = 255;
    uint16_t frameMs = 23, segmentStart = 0, segmentStop = 0;
    void defineControls() override {
        controls_.addControl("speed", speed, 0, 255);
        controls_.addControl("red", red, 0, 255);
        controls_.addControl("green", green, 0, 255);
        controls_.addControl("blue", blue, 0, 255);
        controls_.addControl("secondary red", secondaryRed, 0, 255);
        controls_.addControl("secondary green", secondaryGreen, 0, 255);
        controls_.addControl("secondary blue", secondaryBlue, 0, 255);
        controls_.addControl("frame ms", frameMs, 1, 1000);
        controls_.addControl("segment start", segmentStart);
        controls_.addControl("segment stop", segmentStop);
    }
    void tick() MM_NONBLOCKING override { renderAt(elapsed()); }
    void renderAt(uint32_t now) {
        uint8_t* out = buffer(); const uint32_t count = nrOfLights();
        const uint8_t channels = channelsPerLight();
        if (!out || channels < 3 || !count) return;
        if (!started_ || uint32_t(now - frameAt_) >= (frameMs ? frameMs : 1)) {
            frameAt_ = now; started_ = true;
        }
        const uint32_t first = segmentStart < count ? segmentStart : count;
        const uint32_t last = segmentStop == 0 || segmentStop > count ? count : segmentStop;
        const uint32_t length = last > first ? last - first : 0;
        const uint16_t counter = speed ? uint16_t(frameAt_ * ((speed >> 3) + 1u)) >> 8 : 0;
        for (uint32_t i = 0; i < count; ++i) {
            const bool inside = i >= first && i < last && length;
            uint8_t index = inside ? uint8_t((i-first)*255u/length-counter) : 0;
            if (!speed) index = uint8_t((uint16_t(index)*241u)>>8); // mode 0 does not wrap a stationary palette
            const RGB color = inside ? WledTwoColorPalette::sampleIndex({red,green,blue}, {secondaryRed,secondaryGreen,secondaryBlue}, index, false) : RGB{0,0,0};
            const size_t p = size_t(i)*channels;
            out[p]=color.r; out[p+1]=color.g; out[p+2]=color.b; if (channels>3) out[p+3]=0;
        }
    }
private:
    uint32_t frameAt_ = 0;
    bool started_ = false;
};
} // namespace mm
