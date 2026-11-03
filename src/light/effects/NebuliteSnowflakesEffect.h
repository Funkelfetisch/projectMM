#pragma once

#include "light/effects/EffectBase.h"
#include "light/effects/WledColorTwinkles.h"
#include "core/util/LightOutput.h"
#include <cstring>

namespace mm {
/// Archived WLED-MM ColorTwinkles on a physical strip segment with palette 36.
/// The random stream is local to this effect, rather than WLED's global stream.
/// @attribution WLED, FastLED
class NebuliteSnowflakesEffect : public EffectBase {
public:
    Dim dimensions() const override { return Dim::D3; }
    bool usesPhysicalLightOrder() const override { return true; }
    WorkingMemory workingMemory(Coord3D raster, uint8_t) const override {
        if (raster.x <= 0 || raster.y <= 0 || raster.z <= 0) return {};
        const uint64_t count = uint64_t(raster.x) * raster.y * raster.z;
        if (count > 65535) return {SIZE_MAX, SIZE_MAX};
        return {size_t(count) * (sizeof(RGB) + sizeof(uint8_t)), size_t(count) * sizeof(RGB)};
    }
    uint8_t speed = 35, intensity = 197, sourceBrightness = 255;
    bool followMaster = true;
    uint16_t frameMs = 24, segmentStart = 0, segmentStop = 0;

    void defineControls() override {
        controls_.addControl("speed", speed, 0, 255);
        controls_.addControl("effect intensity", intensity, 0, 255);
        controls_.addControl("source brightness", sourceBrightness, 0, 255);
        controls_.addControl("follow master", followMaster);
        controls_.addControl("frame ms", frameMs, 1, 1000);
        controls_.addControl("segment start", segmentStart);
        controls_.addControl("segment stop", segmentStop);
    }
    void prepare() override {
        const size_t count = nrOfLights();
        const bool sameBuffers = count && pixels_.count() == count && rising_.count() == count;
        if (!count || count > 65535 || !pixels_.resize(count) || !rising_.resize(count)) {
            pixels_.resize(0); rising_.resize(0); started_ = false;
            setStatus("snowflakes memory unavailable", Severity::Warning); return;
        }
        if (!sameBuffers) clearState();
        clearStatus();
    }
    void release() override { started_ = false; MoonModule::release(); }
    void tick() MM_NONBLOCKING override { renderAt(elapsed()); }
    void renderAt(uint32_t now) {
        uint8_t* out = buffer(); const uint32_t count = nrOfLights(); const uint8_t channels = channelsPerLight();
        if (!out || channels < 3 || !count) return;
        std::memset(out, 0, size_t(count) * channels);
        if (pixels_.count() != count || rising_.count() != count) return;
        const uint32_t first = segmentStart < count ? segmentStart : count;
        const uint32_t last = segmentStop == 0 || segmentStop > count ? count : segmentStop;
        if (first != previousStart_ || last != previousStop_) {
            clearState(); previousStart_ = first; previousStop_ = last;
        }
        if (last <= first) return;
        if (!started_ || uint32_t(now - frameAt_) >= (frameMs ? frameMs : 1)) {
            const auto* output = LightOutput::active();
            const uint8_t fadeBrightness = followMaster && output ? output->masterBrightness() : sourceBrightness;
            WledColorTwinkles::step(pixels_.data() + first, rising_.data() + first,
                                    last - first, speed, intensity, fadeBrightness, random_);
            frameAt_ = now; started_ = true;
        }
        for (uint32_t i = first; i < last; ++i) {
            const size_t p = size_t(i) * channels;
            out[p] = pixels_[i].r; out[p + 1] = pixels_[i].g; out[p + 2] = pixels_[i].b;
        }
    }
private:
    struct Random {
        uint16_t seed = 1337;
        uint16_t draw() { seed = uint16_t(seed * 2053u + 13849u); return seed; }
        uint8_t next8() { const uint16_t n = draw(); return uint8_t(uint8_t(n) + uint8_t(n >> 8)); }
        uint16_t bounded16(uint16_t n) { return uint16_t((uint32_t(draw()) * n) >> 16); }
    } random_;
    void clearState() {
        if (pixels_) std::memset(pixels_.data(), 0, pixels_.bytes());
        if (rising_) std::memset(rising_.data(), 0, rising_.bytes());
        started_ = false;
    }
    ScratchBuffer<RGB> pixels_{*this};
    ScratchBuffer<uint8_t> rising_{*this};
    uint32_t frameAt_ = 0, previousStart_ = UINT32_MAX, previousStop_ = UINT32_MAX;
    bool started_ = false;
};
} // namespace mm
