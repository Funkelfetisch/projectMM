#pragma once

#include "light/effects/SphereRing.h"

namespace mm {

/// Every beat sends a soft ring of light from both ends of a line through the ball toward its middle, where the echoes from the two ends meet and fade.
///
/// A ring swells in rather than flashing, and dims as it travels until it is gone, so a fast song layers its echoes instead of blinking. A low glow follows the music between beats.
class SphereBeatEchoEffect : public sphere::RingEffect {
public:
    /// Catalog tags: MoonLight origin, reacts to music.
    const char* tags() const override { return "💫🎵"; }
    /// How fast an echo travels.
    uint8_t speed = 110;
    /// How wide an echo is.
    uint8_t width = 60;
    /// How bright the glow between beats is.
    uint8_t glow = 30;

    /// Publish the speed, the width and the glow.
    void defineControls() override {
        controls_.addControl("speed", speed, 1, 255);
        controls_.addControl("width", width, 10, 160);
        controls_.addControl("glow", glow, 0, 120);
    }

    /// Start an echo on a beat, then draw every echo on its way and the glow beneath them.
    void tick() MM_NONBLOCKING override {
        const uint32_t t = elapsed();
        const uint32_t travel = 3200 - speed * 10;  // ms for an echo to reach the middle
        for (Echo& e : echoes_)
            if (e.born && t - (e.born - 1) >= travel) e.born = 0;
        listen(t);
        const int32_t half = width * 40;
        const int32_t base = glow_.level() * glow / 255;
        const uint8_t baseHue = sphere::brightIndex(static_cast<uint8_t>(t / 700));
        forEachLight([&](uint8_t* out, uint32_t i, uint8_t channels, uint16_t a) {
            const int32_t f = sphere::fromAxis(a, 0);
            const RGB under = sphere::color(static_cast<uint8_t>(baseHue + f / 256), base);
            int32_t rr = under.r, gg = under.g, bb = under.b;
            for (const Echo& e : echoes_) {
                if (!e.born) continue;
                const uint32_t age = t - (e.born - 1);
                const int32_t at = static_cast<int32_t>(static_cast<uint64_t>(age) * 16384 / travel);  // the echo's distance from the ends
                const int32_t off = std::abs(f - at);
                if (off >= half) continue;
                const int32_t left = 255 - static_cast<int32_t>(age * 255 / travel);
                const int32_t swell = std::min<int32_t>(static_cast<int32_t>(age) * 255 / 90, 255);  // in over 90 ms, never a flash
                const int32_t shape = 255 - off * 255 / half;
                const RGB c = sphere::color(e.tint, shape * shape / 255 * left / 255 * swell / 255 * e.strength / 255);
                rr += c.r; gg += c.g; bb += c.b;
            }
            sphere::put(out, i, channels, rr, gg, bb);
        });
    }

private:
    /// One echo under way.
    struct Echo {
        uint32_t born = 0;     ///< when it started, in ms plus one; 0 is an empty slot
        uint8_t tint = 0;      ///< its place in the palette
        uint8_t strength = 0;  ///< how hard the beat was
    };

    /// Follow the glow, and start an echo on a beat at least 180 ms after the last, into a free slot only, so no echo is ever cut off.
    void listen(uint32_t t) {
        int32_t dt = static_cast<int32_t>(t - last_);
        if (dt >= 0 && dt < 10) return;
        if (dt < 0 || dt > 60) dt = 60;
        last_ = t;
        glow_.step(sphere::bands(0, 15) * 2, dt, 40, 8);
        const bool heard = sphere::onBeat();
        if (heard && !wasBeat_ && t - lastEcho_ > 180) {
            for (Echo& e : echoes_) {
                if (e.born) continue;
                e.born = t + 1;
                e.tint = sphere::brightIndex(static_cast<uint8_t>(count_ * 41));
                e.strength = static_cast<uint8_t>(std::clamp<int32_t>(140 + sphere::bands(0, 3), 140, 255));
                ++count_;
                lastEcho_ = t;
                break;
            }
        }
        wasBeat_ = heard;
    }

    static constexpr uint8_t kEchoes = 10;  ///< echoes under way at once; a beat that finds them all busy starts none
    Echo echoes_[kEchoes];
    sphere::Envelope glow_;
    uint32_t last_ = 0;
    uint32_t lastEcho_ = 0;
    uint8_t count_ = 0;
    bool wasBeat_ = false;
};

} // namespace mm
