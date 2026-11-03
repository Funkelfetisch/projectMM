#pragma once

#include "light/effects/EffectBase.h"
#include "light/effects/WledFirePalette.h"
#include "light/effects/WledSineMotion.h"
#include "light/effects/WledTwinkleMotion.h"

namespace mm {
/// Archived WLED-MM Twinkleup (FX 106) with Fire palette 35 and no-wrap mode 0.
///
/// RGB is uncorrected; source gamma and physical output need qualification.
/// @attribution WLED, FastLED, Andrew Tuline
class NebuliteFireflyEffect : public EffectBase {
public:
    /// Spans the whole physical light list, so the effect is volumetric.
    Dim dimensions() const override { return Dim::D3; }
    /// Indexes the real lights in wiring order rather than the layout's bounding box.
    bool usesPhysicalLightOrder() const override { return true; }
    uint8_t speed = 128,       ///< How fast the lights twinkle.
            intensity = 128;   ///< How many lights twinkle at once.
    uint8_t secondaryRed = 0,     ///< The red part of the background color between twinkles.
            secondaryGreen = 0,   ///< The green part of the background color.
            secondaryBlue = 0,    ///< The blue part of the background color.
            secondaryWhite = 0;   ///< The white part of the background color.
    uint16_t frameMs = 23,        ///< The shortest time between frame steps, in milliseconds.
             segmentStart = 0,    ///< The first light of the segment the effect fills.
             segmentStop = 0;     ///< One past the last light of the segment; zero runs to the end.
    /// Publish the WLED speed, intensity, background color, frame time and segment controls.
    void defineControls() override {
        controls_.addControl("speed", speed, 0, 255);
        controls_.addControl("effect intensity", intensity, 0, 255);
        controls_.addControl("secondary red", secondaryRed, 0, 255);
        controls_.addControl("secondary green", secondaryGreen, 0, 255);
        controls_.addControl("secondary blue", secondaryBlue, 0, 255);
        controls_.addControl("secondary white", secondaryWhite, 0, 255);
        controls_.addControl("frame ms", frameMs, 1, 1000);
        controls_.addControl("segment start", segmentStart);
        controls_.addControl("segment stop", segmentStop);
    }
    /// Render one frame at the layer's elapsed time.
    void tick() MM_NONBLOCKING override { renderAt(elapsed()); }
    /// Render the frame for the clock value `now`; public so a test can drive the clock.
    void renderAt(uint32_t now) {
        uint8_t* out = buffer(); const uint32_t count = nrOfLights();
        const uint8_t channels = channelsPerLight();
        if (!out || channels < 3 || !count) return;
        if (!started_ || uint32_t(now - frameAt_) >= (frameMs ? frameMs : 1)) {
            frameAt_ = now; started_ = true;
        }
        const uint32_t first = segmentStart < count ? segmentStart : count;
        const uint32_t last = segmentStop == 0 || segmentStop > count ? count : segmentStop;
        WledTwinkleMotion motion(frameAt_, speed, intensity);
        for (uint32_t i = 0; i < count; ++i) {
            const bool inside = i >= first && i < last;
            const auto pixel = inside ? motion.next() : WledTwinkleMotion::Pixel{};
            const RGB color = inside ? WledFirePalette::sample(pixel.paletteIndex) : RGB{0, 0, 0};
            const size_t p = size_t(i) * channels;
            out[p] = inside ? WledSineMotion::blend(secondaryRed, color.r, pixel.brightness) : 0;
            out[p + 1] = inside ? WledSineMotion::blend(secondaryGreen, color.g, pixel.brightness) : 0;
            out[p + 2] = inside ? WledSineMotion::blend(secondaryBlue, color.b, pixel.brightness) : 0;
            if (channels > 3) out[p + 3] = inside ? WledSineMotion::blend(secondaryWhite, 0, pixel.brightness) : 0;
        }
    }
private:
    uint32_t frameAt_ = 0;
    bool started_ = false;
};
} // namespace mm
