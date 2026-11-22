#pragma once

#include "light/effects/SphereRing.h"

namespace mm {

/// The sphere breathes with the music's low end: a glow opens from both ends of a line through the ball toward its middle as the bass swells, and closes again as it fades.
///
/// The glow follows the bass smoothly, quick to open and slow to let go, so a kick swells it rather than flashing it. It measures the bass against the loudest of late, so it opens fully at any volume. Its color walks slowly along the palette, and the line turns slowly.
class SphereBassBreathEffect : public sphere::RingEffect {
public:
    /// Catalog tags: MoonLight origin, reacts to music, reads the spectrum.
    const char* tags() const override { return "💫🎵🎶"; }
    /// How far the bass opens the glow.
    uint8_t gain = 170;
    /// How slowly the glow closes again.
    uint8_t release = 40;
    /// How fast the line turns.
    uint8_t turn = 20;
    /// The bass a silent room still reads, which never opens the glow.
    uint8_t floor = 5;

    /// Publish the gain, the floor, the release and the turn.
    void defineControls() override {
        controls_.addControl("gain", gain, 10, 255);
        controls_.addControl("floor", floor, 0, 100);
        controls_.addControl("release", release, 1, 255);
        controls_.addControl("turn", turn, 0, 255);
    }

    /// Follow the bass, then draw the glow as far open as it is.
    void tick() MM_NONBLOCKING override {
        const uint32_t t = elapsed();
        listen(t);
        const int32_t open = breath_.level() * 16384 / 255;  // how far from the ends the glow reaches
        const int32_t glow = std::min<int32_t>(breath_.level() * 3, 255);  // and how bright it is, dark only in a silent room
        const uint16_t axis = turn ? sphere::phaseOf(t, std::min<uint32_t>(4000000u / turn, 260000u)) : 0;
        const uint8_t hue = sphere::brightIndex(static_cast<uint8_t>(t / 500));
        forEachLight([&](uint8_t* out, uint32_t i, uint8_t channels, uint16_t a) {
            const int32_t f = sphere::fromAxis(a, axis);
            int32_t v = 0;
            if (f < open) v = 255 - f * 160 / (open + 1);                  // brightest at the ends, softer toward the edge
            else if (f < open + 2400) v = 95 - (f - open) * 95 / 2400;  // and a soft edge, never a hard one
            sphere::put(out, i, channels, sphere::color(static_cast<uint8_t>(hue + f / 512), v * glow / 255));
        });
    }

private:
    /// Take the bass in steps of 10 ms and more, so the envelope keeps its precision at any frame rate.
    void listen(uint32_t t) {
        int32_t dt = static_cast<int32_t>(t - last_);
        if (dt >= 0 && dt < 10) return;
        if (dt < 0 || dt > 60) dt = 60;
        last_ = t;
        // Automatic gain: the bass is measured against the loudest of late, which falls back over some seconds, so the glow opens fully at any volume.
        const int32_t bass = sphere::bands(0, 9);  // the bass and the low mids, which small speakers still play
        const int32_t noise = floor;
        peak_ = std::max<int32_t>({bass << 8, peak_ - peak_ * dt / 4000, (noise + kQuiet) << 8});
        const int32_t heard = std::max<int32_t>(0, bass - noise) * 255 / ((peak_ >> 8) - noise);  // the bass above the room's noise, against the loudest of late
        breath_.step(heard * gain / 100, dt, 90, std::max<int32_t>(1, 256 - release) / 16 + 1);
    }

    static constexpr int32_t kQuiet = 9;  ///< how far above the floor the loudest of late stays at least, so a quiet room's noise is never driven to full
    sphere::Envelope breath_;
    int32_t peak_ = 14 << 8;  ///< the loudest bass of late, with eight fraction bits
    uint32_t last_ = 0;
};

} // namespace mm
