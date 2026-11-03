#pragma once

#include "light/effects/EffectBase.h"
#include "light/effects/WledNoisefireFrame.h"
#include "light/effects/WledLegacyAudio.h"

namespace mm {
/// Experimental physical-order port of archived WLED-MM FX 143.
///
/// Live audio levels are not yet calibrated to the archived controller; optional WLED beat simulation runs only when no AudioService is active; a connected but quiet source stays quiet.
/// @attribution WLED and FastLED contributors
class NebuliteNoisefireEffect : public EffectBase {
public:
    /// Spans the whole physical light list, so the effect is volumetric.
    Dim dimensions() const override { return Dim::D3; }
    /// Indexes the real lights in wiring order rather than the layout's bounding box.
    bool usesPhysicalLightOrder() const override { return true; }
    uint8_t speed = 128,       ///< How fast the flames flicker.
            intensity = 128,   ///< How far the flames burn toward the hot end of the palette.
            audioFloor = 0;    ///< The audio level the flames never dim below.
    /// Simulates the audio level while no audio service runs.
    bool simulateIfNoSource = false;
    uint16_t frameMs = 23,        ///< The shortest time between frame steps, in milliseconds.
             segmentStart = 0,    ///< The first light of the segment the effect fills.
             segmentStop = 0;     ///< One past the last light of the segment; zero runs to the end.

    /// Publish the WLED speed, audio, frame time and segment controls.
    void defineControls() override {
        controls_.addControl("speed", speed, 0, 255);
        controls_.addControl("effect intensity", intensity, 0, 255);
        controls_.addControl("audio floor", audioFloor, 0, 255);
        controls_.addControl("simulate if no source", simulateIfNoSource);
        controls_.addControl("frame ms", frameMs, 1, 1000);
        controls_.addControl("segment start", segmentStart);
        controls_.addControl("segment stop", segmentStop);
    }
    /// Forget the running state so the next enable restarts the flames.
    void release() override { started_ = false; MoonModule::release(); }
    /// Render one frame from the latest smoothed audio level.
    void tick() MM_NONBLOCKING override {
        const auto* audio = AudioService::latestFrame();
        renderAt(elapsed(), audio->levelSmoothed, audio != &AudioService::kSilence);
    }

    /// Render the frame for the clock value `now`; public so a test can drive the clock and the audio level.
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
