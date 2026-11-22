#pragma once

#include "light/effects/SphereRing.h"

#include <cstdlib>

namespace mm {

/// A drop falls on the ring and sends a wave each way round; the two meet straight across the sphere and flare, then run on and close where the drop fell.
///
/// Many drops can be under way at once, each in a color from the palette and each dimming steadily until it is gone. With music the beat lets the drops fall, and between beats they fall by themselves at the rate set.
class SphereAntipodeDropEffect : public sphere::RingEffect {
public:
    /// Catalog tags: MoonLight origin, reacts to music.
    const char* tags() const override { return "💫🎵"; }
    /// How fast the waves run round.
    uint8_t speed = 110;
    /// How often a drop falls by itself.
    uint8_t rate = 150;
    /// How long a wave's wake is.
    uint8_t wake = 70;
    /// With music, a drop falls on every beat.
    bool onBeat = true;

    /// Publish the wave speed, the drop rate, the wake and the beat.
    void defineControls() override {
        controls_.addControl("speed", speed, 1, 255);
        controls_.addControl("rate", rate, 0, 255);
        controls_.addControl("wake", wake, 5, 120);
        controls_.addControl("onBeat", onBeat);
    }

    /// Let the drops fall that are due, then draw every wave and flare.
    void tick() MM_NONBLOCKING override {
        const uint32_t t = elapsed();
        const uint32_t life = lifeMs();
        for (Drop& d : drops_)
            if (d.born && t - (d.born - 1) >= life) d.born = 0;
        fall(t, life);
        const int32_t wakeLength = wake * 100;
        forEachLight([&](uint8_t* out, uint32_t i, uint8_t channels, uint16_t a) {
            int32_t rr = 0, gg = 0, bb = 0;
            for (const Drop& drop : drops_) {
                if (!drop.born) continue;
                const uint32_t age = t - (drop.born - 1);
                const int32_t run = static_cast<int32_t>(static_cast<uint64_t>(age) * 65536 / life);  // how far each wave has come; 32768 is the far side, 65536 home
                const int32_t left = 255 - static_cast<int32_t>(age * 255 / life);
                const int32_t fade = left * left / 255;  // the whole drop dims from its first frame to its last, so nothing ever cuts off
                // One wave runs each way round; a place lies behind a wave by how far that wave has passed it.
                for (const uint16_t ahead : {static_cast<uint16_t>(a - drop.origin), static_cast<uint16_t>(drop.origin - a)}) {
                    const int32_t behind = run - ahead;
                    if (behind < 0 || behind >= wakeLength) continue;
                    const int32_t v = (255 - behind * 255 / wakeLength) * fade / 255;
                    const RGB w = sphere::color(drop.tint, v);
                    rr += w.r; gg += w.g; bb += w.b;
                }
                // The flare: where the drop fell as it lands, and straight across as its two waves meet there.
                int32_t d = static_cast<uint16_t>(a - drop.origin);
                if (d > 32768) d = 65536 - d;  // 0 where it fell, 32768 straight across
                const int32_t landing = 255 - run * 255 / 9000;
                if (landing > 0 && d < 1200) { const int32_t f = landing * (1200 - d) / 1200 * fade / 255; rr += f; gg += f; bb += f; }
                const int32_t meeting = 255 - std::abs(run - 32768) * 255 / 7000;
                if (meeting > 0 && d > 32768 - 1500) { const int32_t f = meeting * (d - 31268) / 1500 * fade / 255; rr += f; gg += f; bb += f; }
            }
            sphere::put(out, i, channels, rr, gg, bb);
        });
    }

private:
    /// One drop under way.
    struct Drop {
        uint16_t origin = 0;  ///< where it fell
        uint32_t born = 0;    ///< when it fell, in ms plus one; 0 is an empty slot
        uint8_t tint = 0;     ///< its place in the palette
    };

    /// How long a drop lasts: its waves go once round and fade out as they come home.
    uint32_t lifeMs() const { return static_cast<uint32_t>(5400 - speed * 16); }

    /// A drop falls when due or on a beat 220 ms after the last, into a free slot only, and before the last has faded, so the ring never goes dark.
    void fall(uint32_t t, uint32_t life) {
        const bool due = t >= nextAt_ || t < lastAt_ || (onBeat && sphere::onBeat() && t - lastAt_ > 220);
        if (!due) return;
        Drop* d = nullptr;
        for (Drop& slot : drops_) if (!slot.born) { d = &slot; break; }
        if (!d) return;
        d->tint = sphere::brightIndex(static_cast<uint8_t>(random() >> 8));
        d->origin = static_cast<uint16_t>(random() & 0xFF00);
        d->born = t + 1;
        lastAt_ = t;
        nextAt_ = t + std::min<uint32_t>(500 + (255 - rate) * 14, life * 3 / 5);
    }

    /// A linear congruential generator: the drops need variety, not quality.
    uint16_t random() {
        seed_ = seed_ * 1664525u + 1013904223u;
        return static_cast<uint16_t>(seed_ >> 16);
    }

    static constexpr uint8_t kDrops = 12;  ///< drops under way at once; a beat that finds them all busy lets none fall
    Drop drops_[kDrops];
    uint32_t nextAt_ = 0;   ///< when the next drop falls by itself
    uint32_t lastAt_ = 0;   ///< when the last one fell
    uint32_t seed_ = 0x2545F491u;
};

} // namespace mm
