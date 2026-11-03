#pragma once

#include "light/effects/EffectBase.h"
#include "light/effects/WledLegacyAudio.h"
#include "light/effects/WledRandomPalette.h"
#include "light/effects/WledSineMotion.h"
#include "light/effects/WledWaterfallFrame.h"

namespace mm {
/// Experimental 1D WLED Waterfall, using the archived single-colour palette.
/// Live onset and magnitude use MoonLight's differently scaled analysis;
/// legacy gamma, physical audio and the random sequence are not calibrated.
/// @attribution WLED and FastLED contributors
class NebuliteWaterfallEffect : public EffectBase {
public:
    Dim dimensions() const override { return Dim::D3; }
    bool usesPhysicalLightOrder() const override { return true; }
    WorkingMemory workingMemory(Coord3D raster, uint8_t) const override {
        if (raster.x <= 0 || raster.y <= 0 || raster.z <= 0) return {};
        const uint64_t count = uint64_t(raster.x) * raster.y * raster.z;
        if (count > 65535) return {SIZE_MAX, SIZE_MAX};
        const size_t bytes = size_t(count) * sizeof(Pixel);
        return {bytes, bytes};
    }
    uint8_t speed = 128, intensity = 128, red = 61, green = 207, blue = 255;
    bool simulateIfNoSource = false;
    uint16_t frameMs = 23, segmentStart = 0, segmentStop = 0;

    void defineControls() override {
        controls_.addControl("speed", speed, 0, 255);
        controls_.addControl("effect intensity", intensity, 0, 255);
        controls_.addControl("red", red, 0, 255);
        controls_.addControl("green", green, 0, 255);
        controls_.addControl("blue", blue, 0, 255);
        controls_.addControl("simulate if no source", simulateIfNoSource);
        controls_.addControl("frame ms", frameMs, 1, 1000);
        controls_.addControl("segment start", segmentStart);
        controls_.addControl("segment stop", segmentStop);
    }
    void prepare() override {
        const size_t count = nrOfLights();
        const bool same = count && pixels_.count() == count;
        if (!count || count > 65535 || !pixels_.resize(count)) {
            pixels_.resize(0); started_ = false;
            setStatus("waterfall memory unavailable", Severity::Warning); return;
        }
        if (!same) clearState();
        clearStatus();
    }
    void release() override { started_ = false; MoonModule::release(); }
    void tick() MM_NONBLOCKING override {
        const auto* audio = AudioService::latestFrame();
        renderAt(elapsed(), platform::micros(), *audio, audio != &AudioService::kSilence);
    }
    void renderAt(uint32_t now, uint32_t micros, const AudioFrame& audio, bool sourcePresent) {
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
        if (!started_ || uint32_t(now - frameAt_) >= (frameMs ? frameMs : 1)) {
            bool peak = audio.onset != 0; float frequency = audio.peakHz; uint8_t magnitude = uint8_t(audio.peakMag > 255 ? 255 : audio.peakMag);
            if (!sourcePresent && simulateIfNoSource) {
                const auto simulated = WledLegacyAudio::peak(WledLegacyAudio::volume(now), rng_.next8());
                peak = simulated.peak; frequency = simulated.frequencyHz; magnitude = uint8_t(simulated.magnitude / 8.0f);
            }
            const RGB highlight = WledRandomPalette::rainbow({92, 92, 92});
            const Pixel color{red, green, blue, 0};
            frame_.step(pixels_.data() + first, last - first, micros, speed, intensity, peak, frequency, magnitude,
                        Pixel{0, 0, 0, 0}, Pixel{highlight.r, highlight.g, highlight.b, 0},
                        [color](uint8_t) { return color; }, [](Pixel a, Pixel b, uint8_t amount) {
                            return Pixel{WledSineMotion::blend(a.r, b.r, amount), WledSineMotion::blend(a.g, b.g, amount),
                                         WledSineMotion::blend(a.b, b.b, amount), WledSineMotion::blend(a.w, b.w, amount)};
                        });
            frameAt_ = now; started_ = true;
        }
        for (uint32_t i = first; i < last; ++i) {
            const size_t p = size_t(i) * channels;
            out[p] = pixels_[i].r; out[p + 1] = pixels_[i].g; out[p + 2] = pixels_[i].b;
            if (channels > 3) out[p + 3] = pixels_[i].w;
        }
    }
private:
    struct Pixel { uint8_t r, g, b, w; };
    void clearState() {
        if (pixels_) std::memset(pixels_.data(), 0, pixels_.bytes());
        frame_.reset(); started_ = false;
    }
    ScratchBuffer<Pixel> pixels_{*this};
    WledWaterfallFrame frame_;
    Random8 rng_;
    uint32_t frameAt_ = 0, previousStart_ = UINT32_MAX, previousStop_ = UINT32_MAX;
    bool started_ = false;
};
} // namespace mm
