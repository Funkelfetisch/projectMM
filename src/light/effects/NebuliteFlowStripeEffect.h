#pragma once

#include "light/effects/EffectBase.h"
#include "light/effects/WledFlowStripeMotion.h"
#include "light/effects/WledRandomPalette.h"
#include <cstring>

namespace mm {
/// Archived WLED Flow Stripe on a selected physical-order wearable segment.
/// @attribution WLED contributors, ldirko, Andrew Tuline, FastLED contributors
class NebuliteFlowStripeEffect : public EffectBase {
public:
    Dim dimensions() const override { return Dim::D3; }
    bool usesPhysicalLightOrder() const override { return true; }
    uint8_t speed = 255, intensity = 255;
    uint16_t frameMs = 23, segmentStart = 0, segmentStop = 0;
    void defineControls() override {
        controls_.addControl("speed", speed, 0, 255);
        controls_.addControl("effect intensity", intensity, 0, 255);
        controls_.addControl("frame ms", frameMs, 1, 1000);
        controls_.addControl("segment start", segmentStart);
        controls_.addControl("segment stop", segmentStop);
    }
    void release() override { started_ = false; MoonModule::release(); }
    void tick() MM_NONBLOCKING override { renderAt(elapsed()); }
    void renderAt(uint32_t now) {
        uint8_t* out = buffer(); const uint32_t count = nrOfLights(); const uint8_t channels = channelsPerLight();
        if (!out || channels < 3 || !count) return;
        std::memset(out, 0, size_t(count) * channels);
        const uint32_t first = segmentStart < count ? segmentStart : count;
        const uint32_t last = segmentStop == 0 || segmentStop > count ? count : segmentStop;
        if (last <= first || last - first > 65535) return;
        if (!started_ || uint32_t(now - frameAt_) >= (frameMs ? frameMs : 1)) {
            frameAt_ = now; started_ = true;
        }
        for (uint32_t i = first; i < last; ++i) {
            uint8_t hue = 0;
            if (!WledFlowStripeMotion::hue(frameAt_, speed, intensity, uint16_t(i - first), uint16_t(last - first), hue)) continue;
            const RGB color = WledRandomPalette::rainbow({hue, 255, 255});
            const size_t p = size_t(i) * channels;
            out[p] = color.r; out[p + 1] = color.g; out[p + 2] = color.b;
        }
    }
private:
    uint32_t frameAt_ = 0;
    bool started_ = false;
};
} // namespace mm
