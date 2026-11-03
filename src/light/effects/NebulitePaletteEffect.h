#pragma once

#include "light/effects/EffectBase.h"
#include "light/effects/WledTwoColorPalette.h"

namespace mm {
/// Archived WLED-MM Palette (FX 65), palette 3, moving-wrap mode 0.
///
/// RGB output is uncorrected; WLED gamma, transitions and physical output need qualification.
class NebulitePaletteEffect : public EffectBase {
public:
    /// Declares the full 3D set; the render indexes lights directly.
    Dim dimensions() const override { return Dim::D3; }
    /// Indexes the real lights in wiring order rather than the layout's bounding box.
    bool usesPhysicalLightOrder() const override { return true; }
    /// How fast the palette slides along the strip.
    uint8_t speed = 128;
    /// Primary color, red part.
    uint8_t red = 0;
    /// Primary color, green part.
    uint8_t green = 255;
    /// Primary color, blue part.
    uint8_t blue = 200;
    /// Secondary color, red part.
    uint8_t secondaryRed = 255;
    /// Secondary color, green part.
    uint8_t secondaryGreen = 0;
    /// Secondary color, blue part.
    uint8_t secondaryBlue = 255;
    /// WLED frame time in milliseconds, which paces the palette's motion.
    uint16_t frameMs = 23;
    /// First light of the segment.
    uint16_t segmentStart = 0;
    /// End of the segment, exclusive; zero means the end of the strip.
    uint16_t segmentStop = 0;
    /// Publish the speed, both colors, the frame time and the segment.
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
    /// Render one frame at the effect's own clock.
    void tick() MM_NONBLOCKING override { renderAt(elapsed()); }
    /// Draw the palette across the segment as of `now`, advancing it once `frameMs` has passed, and black elsewhere.
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
