#pragma once

#include "light/effects/SphereRing.h"

namespace mm {

/// Daylight on one half of the ring, night on the other, and the edge between them travelling round: a sunrise at one place is a sunset straight across the sphere.
///
/// Day is a warm white, whitest at noon; dawn and dusk run from night blue through red to gold; in the night small lights come on, the more the further from dusk. Its colors are its own, so it ignores the palette.
class SphereDayAndNightEffect : public sphere::RingEffect {
public:
    /// Catalog tags: MoonLight origin.
    const char* tags() const override { return "💫"; }
    /// Seconds for the sun to go once round.
    uint8_t day = 30;
    /// How many lights come on at night.
    uint8_t lights = 120;
    /// How wide dawn and dusk are.
    uint8_t dusk = 40;

    /// Publish the length of a day, the night's lights and the width of dusk.
    void defineControls() override {
        controls_.addControl("day", day, 4, 240);
        controls_.addControl("lights", lights, 0, 255);
        controls_.addControl("dusk", dusk, 10, 120);
    }

    /// Light each place by the sun's height there.
    void tick() MM_NONBLOCKING override {
        const uint32_t t = elapsed();
        const uint16_t sun = sphere::phaseOf(t, uint32_t(day) * 1000);  // where noon is
        const int32_t edge = dusk * 200;
        forEachLight([&](uint8_t* out, uint32_t i, uint8_t channels, uint16_t a) {
            const int32_t high = sphere::cosine(static_cast<uint16_t>(a - sun)) - 32768;  // 32767 at noon, -32768 at midnight
            int32_t rr = 0, gg = 0, bb = 0;
            if (high > edge) {
                // Day: a warm white, whitest at noon.
                const int32_t k = (high - edge) * 255 / (32768 - edge);
                rr = 255; gg = 150 + k * 80 / 255; bb = 50 + k * 130 / 255;
            } else if (high > -edge) {
                // Dawn and dusk: from night blue through red to the day's first gold.
                const int32_t k = (high + edge) * 255 / (2 * edge);
                if (k < 128) { rr = 6 + k * 249 / 128; gg = 4 + k * 36 / 128; bb = 36 - k * 26 / 128; }
                else { rr = 255; gg = 40 + (k - 128) * 110 / 127; bb = 10 + (k - 128) * 40 / 127; }
            } else {
                // Night: deep blue, and the lights that come on in it.
                const int32_t deep = std::min<int32_t>((-edge - high) / 64, 255);
                rr = 0; gg = 2; bb = 14 + deep / 12;
                const uint32_t x = uint32_t(a) * 4;
                const int32_t town = inoise8(x, 0, 0) + lights - 255;
                if (town > 0 && deep > 20) {
                    int32_t glow = std::min<int32_t>(town * 3, 255) * (130 + inoise8(x, t / 3, 9000) / 2) / 255;
                    glow = glow * std::min<int32_t>(deep * 4, 255) / 255;
                    rr += glow; gg += glow * 170 / 255; bb += glow * 60 / 255;
                }
            }
            sphere::put(out, i, channels, rr, gg, bb);
        });
    }
};

} // namespace mm
