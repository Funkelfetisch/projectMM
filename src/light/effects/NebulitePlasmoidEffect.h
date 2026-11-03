#pragma once

#include "light/effects/EffectBase.h"
#include "light/effects/WledPlasmoidFrame.h"
#include "light/effects/WledLegacyAudio.h"

namespace mm {
/// Experimental wearable port of WLED-MM FX 133 with random palette 74.
///
/// Audio level scaling, local random stream and clock are not calibrated to an archived physical controller. Silence stays dark with the saved black secondary color; optional simulation applies only with no audio service.
/// @attribution WLED and FastLED contributors
class NebulitePlasmoidEffect : public EffectBase {
public:
    /// Spans the whole physical light list, so the effect is volumetric.
    Dim dimensions() const override { return Dim::D3; }
    /// Indexes the real lights in wiring order rather than the layout's bounding box.
    bool usesPhysicalLightOrder() const override { return true; }
    /// One plasmoid pixel per light as a single block, refused above 65535 lights.
    WorkingMemory workingMemory(Coord3D raster, uint8_t) const override {
        if (raster.x <= 0 || raster.y <= 0 || raster.z <= 0) return {};
        const uint64_t count = uint64_t(raster.x) * raster.y * raster.z;
        if (count > 65535) return {SIZE_MAX, SIZE_MAX};
        const size_t bytes = size_t(count) * sizeof(WledPlasmoidFrame::Pixel);
        return {bytes, bytes};
    }
    uint8_t speed = 41,        ///< How tightly the plasma waves pack along the strip.
            intensity = 112,   ///< How much of each wave a given audio level reveals.
            audioFloor = 0;    ///< The lowest audio level the waves are drawn from.
    /// Simulates the audio level while no audio service runs.
    bool simulateIfNoSource = false;
    uint8_t secondaryRed = 0,     ///< The red part of the secondary color the waves fade into.
            secondaryGreen = 0,   ///< The green part of the secondary color.
            secondaryBlue = 0,    ///< The blue part of the secondary color.
            secondaryWhite = 0;   ///< The white part of the secondary color.
    uint16_t frameMs = 23,        ///< The shortest time between frame steps, in milliseconds.
             segmentStart = 0,    ///< The first light of the segment the effect fills.
             segmentStop = 0;     ///< One past the last light of the segment; zero runs to the end.

    /// Publish the WLED speed, audio, secondary color, frame time and segment controls.
    void defineControls() override {
        controls_.addControl("speed", speed, 0, 255);
        controls_.addControl("effect intensity", intensity, 0, 255);
        controls_.addControl("audio floor", audioFloor, 0, 255);
        controls_.addControl("simulate if no source", simulateIfNoSource);
        controls_.addControl("secondary red", secondaryRed, 0, 255);
        controls_.addControl("secondary green", secondaryGreen, 0, 255);
        controls_.addControl("secondary blue", secondaryBlue, 0, 255);
        controls_.addControl("secondary white", secondaryWhite, 0, 255);
        controls_.addControl("frame ms", frameMs, 1, 1000);
        controls_.addControl("segment start", segmentStart);
        controls_.addControl("segment stop", segmentStop);
    }
    /// Size the pixel scratch to the lights and clear the state; a size it cannot hold raises a warning.
    void prepare() override {
        const size_t count = nrOfLights();
        const bool same = count && pixels_.count() == count;
        if (!count || count > 65535 || !pixels_.resize(count)) {
            pixels_.resize(0); started_ = false;
            setStatus("plasmoid memory unavailable", Severity::Warning); return;
        }
        if (!same) clearState();
        clearStatus();
    }
    /// Forget the running state so the next enable restarts the plasma.
    void release() override { started_ = false; MoonModule::release(); }
    /// Render one frame from the latest smoothed audio level.
    void tick() MM_NONBLOCKING override {
        const auto* audio = AudioService::latestFrame();
        renderAt(elapsed(), audio->levelSmoothed, audio != &AudioService::kSilence);
    }

    /// Render the frame for the clock value `now`; public so a test can drive the clock and the audio level.
    void renderAt(uint32_t now, uint16_t audioLevel, bool sourcePresent = true) {
        uint8_t* out = buffer(); const uint32_t count = nrOfLights(); const uint8_t channels = channelsPerLight();
        if (!out || channels < 3 || !count) return;
        std::memset(out, 0, size_t(count) * channels);
        if (pixels_.count() != count) return;
        const uint32_t first = segmentStart < count ? segmentStart : count;
        const uint32_t last = segmentStop == 0 || segmentStop > count ? count : segmentStop;
        if (first != previousStart_ || last != previousStop_) {
            clearState(); previousStart_ = first; previousStop_ = last;
        }
        if (last <= first) return;
        if (!started_) {
            makePalette(current_);
            for (unsigned i = 0; i < 16; ++i) previous_[i] = current_[i];
            paletteAt_ = now;
        } else if (uint32_t(now - paletteAt_) > 5000u) {
            for (unsigned i = 0; i < 16; ++i) previous_[i] = current_[i];
            makePalette(current_); paletteAt_ = now;
        }
        if (!started_ || uint32_t(now - frameAt_) >= (frameMs ? frameMs : 1)) {
            RGB palette[16];
            WledRandomPalette::transition74(previous_, current_, uint32_t(now - paletteAt_), palette);
            if (!sourcePresent && simulateIfNoSource) audioLevel = WledLegacyAudio::volume(now);
            const uint16_t level = audioLevel > audioFloor ? audioLevel : audioFloor;
            frame_.step(pixels_.data() + first, last - first, palette,
                        {secondaryRed, secondaryGreen, secondaryBlue, secondaryWhite},
                        now, speed, intensity, float(level));
            frameAt_ = now; started_ = true;
        }
        for (uint32_t i = first; i < last; ++i) {
            const size_t p = size_t(i) * channels;
            out[p] = pixels_[i].r; out[p + 1] = pixels_[i].g; out[p + 2] = pixels_[i].b;
            if (channels > 3) out[p + 3] = pixels_[i].w;
        }
    }
private:
    void makePalette(RGB (&target)[16]) {
        WledRandomPalette::HSV colors[4];
        for (auto& color : colors) color = {rng_.next8(), rng_.below(160, 255), rng_.below(128, 255)};
        WledRandomPalette::gradient(colors, target);
    }
    void clearState() {
        if (pixels_) std::memset(pixels_.data(), 0, pixels_.bytes());
        frame_ = WledPlasmoidFrame{}; started_ = false;
    }
    ScratchBuffer<WledPlasmoidFrame::Pixel> pixels_{*this};
    WledPlasmoidFrame frame_;
    Random8 rng_;
    RGB previous_[16]{}, current_[16]{};
    uint32_t frameAt_ = 0, paletteAt_ = 0, previousStart_ = UINT32_MAX, previousStop_ = UINT32_MAX;
    bool started_ = false;
};
} // namespace mm
