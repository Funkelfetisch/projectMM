#pragma once

#include "light/effects/EffectBase.h"
#include "light/effects/WledWashingMotion.h"
#include "light/effects/WledRandomPalette.h"
#include "core/util/math8.h"

namespace mm {

/// Experimental physical-strip port of WLED Washing Machine, palette 1.
///
/// Its phase and palette math match WLED, but random seed, initial palette, missed frames and global palette sharing differ; it is not a qualified restoration of the archived preset or its original output curve.
class NebuliteWashingEffect : public EffectBase {
public:
    /// Declares the full 3D set; the render indexes lights directly.
    Dim dimensions() const override { return Dim::D3; }
    /// Indexes the real lights in wiring order rather than the layout's bounding box.
    bool usesPhysicalLightOrder() const override { return true; }
    /// How fast the washing waves move.
    uint8_t speed = 128;
    /// How many waves span the segment.
    uint8_t intensity = 128;
    /// WLED frame time in milliseconds, which paces the wave's motion.
    uint16_t frameMs = 23;
    /// First light of the segment.
    uint16_t segmentStart = 0;
    /// End of the segment, exclusive; zero means the end of the strip.
    uint16_t segmentStop = 0;

    /// Publish the speed, wave count, frame time and segment.
    void defineControls() override {
        controls_.addControl("speed", speed, 0, 255);
        controls_.addControl("effect intensity", intensity, 0, 255);
        controls_.addControl("frame ms", frameMs, 1, 1000);
        controls_.addControl("segment start", segmentStart);
        controls_.addControl("segment stop", segmentStop);
    }

    /// Render one frame at the effect's own clock.
    void tick() MM_NONBLOCKING override { renderAt(elapsed()); }

    /// Draw the washing waves across the segment as of `now`, drifting the palette every five seconds, and black elsewhere.
    void renderAt(uint32_t now) {
        uint8_t* out = buffer();
        const uint32_t count = nrOfLights();
        const uint8_t channels = channelsPerLight();
        if (!out || channels < 3 || !count) return;
        if (!started_) {
            makePalette(current_);
            for (uint8_t i = 0; i < 16; ++i) previous_[i] = current_[i];
            paletteStart_ = now;
            lastFrame_ = now;
            started_ = true;
            phase_ = WledWashingMotion::advance(phase_, now, speed);
        } else {
            if (uint32_t(now - paletteStart_) > 5000u) {
                for (uint8_t i = 0; i < 16; ++i) previous_[i] = current_[i];
                makePalette(current_);
                paletteStart_ = now;
            }
            if (uint32_t(now - lastFrame_) >= (frameMs ? frameMs : 1)) {
                phase_ = WledWashingMotion::advance(phase_, now, speed);
                lastFrame_ = now;
            }
        }
        RGB palette[16];
        for (uint8_t i = 0; i < 16; ++i)
            palette[i] = WledRandomPalette::blend(previous_[i], current_[i], uint32_t(now - paletteStart_));
        const uint32_t first = segmentStart < count ? segmentStart : count;
        const uint32_t last = segmentStop == 0 || segmentStop > count ? count : segmentStop;
        for (uint32_t i = 0; i < count; ++i) {
            const bool inside = i >= first && i < last;
            const uint8_t wave = inside ? WledWashingMotion::sine(WledWashingMotion::waveInput(phase_, i - first, last - first, intensity)) : 0;
            const RGB color = inside ? WledRandomPalette::sample(palette, wave) : RGB{0, 0, 0};
            const size_t p = size_t(i) * channels;
            out[p] = color.r; out[p + 1] = color.g; out[p + 2] = color.b;
            if (channels > 3) out[p + 3] = 0;
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
    uint32_t phase_ = 0, paletteStart_ = 0, lastFrame_ = 0;
    bool started_ = false;
};
} // namespace mm
