#pragma once

#include "light/effects/SphereRing.h"

namespace mm {

/// The music laid round the ring four times over: the bass at both ends of a line through the sphere, the treble crosswise to it, so every band faces itself across the sphere.
///
/// The line turns slowly and every beat pushes it on and flashes its ends white. Each band falls back slowly between hits, and a silent room goes dark unless an idle glow is set.
class SphereSpectrumHaloEffect : public sphere::RingEffect {
public:
    /// Catalog tags: MoonLight origin, reacts to music, reads the spectrum.
    const char* tags() const override { return "💫🎵🎶"; }
    /// How hard the bands are driven.
    uint8_t gain = 97;
    /// How fast a band falls back.
    uint8_t fall = 34;
    /// The glow left when the room is silent.
    uint8_t idle = 0;
    /// How far a beat pushes the line round.
    uint8_t kick = 32;

    /// Publish the gain, the fall, the idle glow and the kick.
    void defineControls() override {
        controls_.addControl("gain", gain, 10, 255);
        controls_.addControl("fall", fall, 1, 255);
        controls_.addControl("idle", idle, 0, 80);
        controls_.addControl("kick", kick, 0, 255);
    }

    /// Follow the bands and the beat, then lay the spectrum round the ring.
    void tick() MM_NONBLOCKING override {
        const uint32_t t = elapsed();
        listen(t);
        const uint8_t near = sphere::brightIndex(static_cast<uint8_t>(t / 400));
        forEachLight([&](uint8_t* out, uint32_t i, uint8_t channels, uint16_t a) {
            const int32_t f = sphere::fromAxis(a, axis_);
            const int32_t band = f * 15 / 16385;  // which band this place shows,
            const int32_t part = f * 15 / 64 - band * 256;  // and how far it is on the way to the next
            int32_t v = (level_[band] * (256 - part) + level_[std::min<int32_t>(band + 1, 15)] * part) / 256;
            v = std::max<int32_t>(v * v / 255, idle);
            const RGB col = sphere::color(static_cast<uint8_t>(near + f / 256), v);  // bass in one color, treble a quarter of the palette on
            const int32_t white = f < 2600 ? hit_ * (2600 - f) / 2600 : 0;
            sphere::put(out, i, channels, col.r + white, col.g + white, col.b + white);
        });
    }

private:
    /// Take the bands and the beat in steps of 10 ms and more, so the fall keeps its precision at any frame rate.
    void listen(uint32_t t) {
        int32_t dt = static_cast<int32_t>(t - last_);
        if (dt >= 0 && dt < 10) return;
        if (dt < 0 || dt > 60) dt = 60;
        last_ = t;
        const bool heard = sphere::onBeat();
        if (heard && !wasBeat_) { hit_ = 255; axis_ = static_cast<uint16_t>(axis_ + kick * 24); }
        wasBeat_ = heard;
        hit_ = std::max<int32_t>(hit_ - dt * 2, 0);
        axis_ = static_cast<uint16_t>(axis_ + dt * 3);
        const AudioFrame* audio = AudioService::latestFrame();
        for (uint8_t i = 0; i < 16; ++i) {
            const int32_t now = std::min<int32_t>(audio->bands[i] * gain / 100, 255);
            level_[i] = static_cast<uint8_t>(std::max<int32_t>(now, level_[i] - dt * fall / 32));
        }
    }

    uint8_t level_[16] = {};  ///< each band's level, falling back between hits
    uint16_t axis_ = 0;       ///< where the bass sits
    uint32_t last_ = 0;
    int32_t hit_ = 0;         ///< the beat's flash, fading
    bool wasBeat_ = false;
};

} // namespace mm
