#pragma once

#include "light/effects/EffectBase.h"
#include "light/effects/WledRandomPalette.h"
#include "light/effects/WledSineMotion.h"

namespace mm {
/// Physical-strip Sine (WLED-MM FX 108) with palette 1.
///
/// The random seed, palette lifetime and clock are local to this effect; physical equivalence with a restored archived device remains unverified.
class NebuliteSineEffect : public EffectBase {
public:
    /// Declares the full 3D set; the render indexes lights directly.
    Dim dimensions() const override { return Dim::D3; }
    /// Indexes the real lights in wiring order rather than the layout's bounding box.
    bool usesPhysicalLightOrder() const override { return true; }
    /// How fast the wave moves along the strip.
    uint8_t speed = 128;
    /// How many waves fit along the strip.
    uint8_t intensity = 128;
    /// Red part of the secondary color shown between waves.
    uint8_t secondaryRed = 0;
    /// Green part of the secondary color shown between waves.
    uint8_t secondaryGreen = 0;
    /// Blue part of the secondary color shown between waves.
    uint8_t secondaryBlue = 0;
    /// White part of the secondary color shown between waves.
    uint8_t secondaryWhite = 0;
    /// Time between simulation steps, in milliseconds.
    uint16_t frameMs = 23;
    /// First light of the segment the effect fills.
    uint16_t segmentStart = 0;
    /// End of the segment, exclusive; zero means the end of the strip.
    uint16_t segmentStop = 0;

    /// Publish the wave's speed and density, the secondary color, the frame time and the segment.
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
    /// Render one frame at the effect's own clock.
    void tick() MM_NONBLOCKING override { renderAt(elapsed()); }

    /// Render one frame at `now`, public so tests can drive the clock.
    void renderAt(uint32_t now) {
        uint8_t* out = buffer();
        const uint32_t count = nrOfLights();
        const uint8_t channels = channelsPerLight();
        if (!out || channels < 3 || !count) return;
        if (!started_) {
            makePalette(current_);
            for (uint8_t i = 0; i < 16; ++i) previous_[i] = current_[i];
            paletteStart_ = now; lastFrame_ = now; started_ = true;
            step_ = WledSineMotion::advance(step_, speed);
        } else if (uint32_t(now - lastFrame_) >= (frameMs ? frameMs : 1)) {
            if (uint32_t(now - paletteStart_) > 5000u) {
                for (uint8_t i = 0; i < 16; ++i) previous_[i] = current_[i];
                makePalette(current_); paletteStart_ = now;
            }
            step_ = WledSineMotion::advance(step_, speed); lastFrame_ = now;
        }
        RGB palette[16];
        for (uint8_t i = 0; i < 16; ++i)
            palette[i] = WledRandomPalette::blend(previous_[i], current_[i], uint32_t(lastFrame_ - paletteStart_));
        const uint32_t first = segmentStart < count ? segmentStart : count;
        const uint32_t last = segmentStop == 0 || segmentStop > count ? count : segmentStop;
        for (uint32_t i = 0; i < count; ++i) {
            const bool inside = i >= first && i < last;
            const uint8_t amount = inside ? WledSineMotion::wave(step_, i - first, intensity) : 0;
            const RGB color = inside ? WledRandomPalette::sample(palette, WledSineMotion::paletteIndex(lastFrame_, i - first)) : RGB{0, 0, 0};
            const size_t p = size_t(i) * channels;
            out[p] = inside ? WledSineMotion::blend(secondaryRed, color.r, amount) : 0;
            out[p + 1] = inside ? WledSineMotion::blend(secondaryGreen, color.g, amount) : 0;
            out[p + 2] = inside ? WledSineMotion::blend(secondaryBlue, color.b, amount) : 0;
            if (channels > 3) out[p + 3] = inside ? WledSineMotion::blend(secondaryWhite, 0, amount) : 0;
        }
    }
private:
    void makePalette(RGB (&target)[16]) {
        WledRandomPalette::HSV colors[4];
        for (auto& c : colors) c = {rng_.next8(), rng_.below(160, 255), rng_.below(128, 255)};
        WledRandomPalette::gradient(colors, target);
    }
    Random8 rng_;
    RGB previous_[16]{}, current_[16]{};
    uint32_t step_ = 0, paletteStart_ = 0, lastFrame_ = 0;
    bool started_ = false;
};
} // namespace mm
