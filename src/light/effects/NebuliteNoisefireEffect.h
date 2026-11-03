#pragma once

#include "light/effects/EffectBase.h"
#include "light/effects/WledNoisefireFrame.h"
#include "light/effects/WledLegacyAudio.h"

namespace mm {
/// Experimental physical-order port of archived WLED-MM FX 143.
/// Live audio levels are not yet calibrated to the archived controller;
/// Optional WLED beat simulation runs only when no AudioService is active;
/// a connected but quiet source stays quiet.
/// @attribution WLED and FastLED contributors
class NebuliteNoisefireEffect : public EffectBase {
public:
    Dim dimensions() const override { return Dim::D3; }
    bool usesPhysicalLightOrder() const override { return true; }
    uint8_t speed = 128, intensity = 128, audioFloor = 0;
    bool simulateIfNoSource = false;
    uint16_t frameMs = 23, segmentStart = 0, segmentStop = 0;

    void defineControls() override {
        controls_.addControl("speed", speed, 0, 255);
        controls_.addControl("effect intensity", intensity, 0, 255);
        controls_.addControl("audio floor", audioFloor, 0, 255);
        controls_.addControl("simulate if no source", simulateIfNoSource);
        controls_.addControl("frame ms", frameMs, 1, 1000);
        controls_.addControl("segment start", segmentStart);
        controls_.addControl("segment stop", segmentStop);
    }
    void release() override { started_ = false; MoonModule::release(); }
    void tick() MM_NONBLOCKING override {
        const auto* audio = AudioService::latestFrame();
        renderAt(elapsed(), audio->levelSmoothed, audio != &AudioService::kSilence);
    }

    void renderAt(uint32_t now, uint16_t audioLevel, bool sourcePresent = true) {
        uint8_t* out = buffer();
        const uint32_t count = nrOfLights();
        const uint8_t channels = channelsPerLight();
        if (!out || channels < 3 || !count) return;
        std::memset(out, 0, size_t(count) * channels);
        const uint32_t first = segmentStart < count ? segmentStart : count;
        const uint32_t last = segmentStop == 0 || segmentStop > count ? count : segmentStop;
        if (last <= first) return;
        if (!started_ || uint32_t(now - frameAt_) >= (frameMs ? frameMs : 1)) {
            frameAt_ = now;
            if (!sourcePresent && simulateIfNoSource) audioLevel = WledLegacyAudio::volume(now);
            const uint16_t level = audioLevel > audioFloor ? audioLevel : audioFloor;
            brightness_ = level >= 128 ? 255 : uint8_t(level * 2u);
            started_ = true;
        }
        for (uint32_t i = first; i < last; ++i) {
            const RGB c = WledNoisefireFrame::pixel(i - first, last - first, speed, intensity, frameAt_, brightness_);
            const size_t p = size_t(i) * channels;
            out[p] = c.r; out[p + 1] = c.g; out[p + 2] = c.b;
        }
    }
private:
    uint32_t frameAt_ = 0;
    uint8_t brightness_ = 0;
    bool started_ = false;
};
} // namespace mm
