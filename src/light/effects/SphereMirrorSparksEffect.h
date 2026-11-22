#pragma once

#include "light/effects/SphereRing.h"

namespace mm {

/// The treble lights sparks that glow up and fade, each at four places at once, mirrored across the line through the ball and repeated straight across, over a bass glow at the line's ends.
///
/// Brighter treble lights more sparks. A spark swells in and dims away smoothly, so the sphere glitters with the music rather than blinking.
class SphereMirrorSparksEffect : public sphere::RingEffect {
public:
    /// Catalog tags: MoonLight origin, reacts to music, reads the spectrum.
    const char* tags() const override { return "💫🎵🎶"; }
    /// How many sparks the treble lights.
    uint8_t density = 120;
    /// How long a spark glows.
    uint8_t decay = 60;
    /// How bright the bass glow is.
    uint8_t glow = 40;

    /// Publish the density, the decay and the glow.
    void defineControls() override {
        controls_.addControl("density", density, 0, 255);
        controls_.addControl("decay", decay, 1, 255);
        controls_.addControl("glow", glow, 0, 160);
    }

    /// Light the sparks the treble calls for, then draw them and the bass glow.
    void tick() MM_NONBLOCKING override {
        const uint32_t t = elapsed();
        const uint32_t life = 300 + decay * 12;  // ms a spark glows
        for (Spark& s : sparks_)
            if (s.born && t - (s.born - 1) >= life) s.born = 0;
        listen(t);
        const int32_t base = bass_.level() * glow / 160;
        const uint8_t baseHue = sphere::brightIndex(static_cast<uint8_t>(t / 900));
        forEachLight([&](uint8_t* out, uint32_t i, uint8_t channels, uint16_t a) {
            const int32_t f = sphere::fromAxis(a, 0);
            const int32_t near = base * std::max<int32_t>(0, 8000 - f) / 8000;  // the bass glows at the line's ends
            const RGB under = sphere::color(baseHue, near);
            int32_t rr = under.r, gg = under.g, bb = under.b;
            for (const Spark& s : sparks_) {
                if (!s.born) continue;
                const int32_t off = std::abs(f - s.at);
                if (off >= 500) continue;  // a spark is a light or two wide
                const uint32_t age = t - (s.born - 1);
                const int32_t swell = std::min<int32_t>(static_cast<int32_t>(age) * 255 / 60, 255);
                const int32_t left = 255 - static_cast<int32_t>(age * 255 / life);
                const RGB c = sphere::color(s.tint, (500 - off) * 255 / 500 * swell / 255 * left / 255 * left / 255);
                rr += c.r; gg += c.g; bb += c.b;
            }
            sphere::put(out, i, channels, rr, gg, bb);
        });
    }

private:
    /// One spark, at a distance from the line, so it shows at that distance on all four sides.
    struct Spark {
        int32_t at = 0;     ///< its distance from the line's ends, 0..16384
        uint32_t born = 0;  ///< when it lit, in ms plus one; 0 is an empty slot
        uint8_t tint = 0;   ///< its place in the palette
    };

    /// Follow the bass, and light sparks in steps of 10 ms as the treble asks, into free slots only.
    void listen(uint32_t t) {
        int32_t dt = static_cast<int32_t>(t - last_);
        if (dt >= 0 && dt < 10) return;
        if (dt < 0 || dt > 60) dt = 60;
        last_ = t;
        bass_.step(sphere::bands(0, 3) * 2, dt, 60, 10);
        owed_ += sphere::bands(9, 15) * density * dt / 10;  // sparks owed, in 1/65536 of a spark
        while (owed_ >= 65536) {
            owed_ -= 65536;
            Spark* free = nullptr;
            for (Spark& s : sparks_) if (!s.born) { free = &s; break; }
            if (!free) { owed_ = 0; break; }
            free->at = random() % 16385;
            free->tint = sphere::brightIndex(static_cast<uint8_t>(random() >> 8));
            free->born = t + 1;
        }
    }

    /// A linear congruential generator: the sparks need variety, not quality.
    uint16_t random() {
        seed_ = seed_ * 1664525u + 1013904223u;
        return static_cast<uint16_t>(seed_ >> 16);
    }

    static constexpr uint8_t kSparks = 24;  ///< sparks lit at once; the treble waits while they are all glowing
    Spark sparks_[kSparks];
    sphere::Envelope bass_;
    int32_t owed_ = 0;
    uint32_t last_ = 0;
    uint32_t seed_ = 0x6C8E9CF5u;
};

} // namespace mm
