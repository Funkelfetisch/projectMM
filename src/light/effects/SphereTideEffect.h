#pragma once

#include "light/effects/SphereRing.h"

namespace mm {

/// Two swells straight across from each other, in two colors, drawn slowly round the ring as the moon draws the sea.
///
/// A second, slower pull makes them rise high when the two line up and sink low when they stand crosswise: spring tide and neap tide.
class SphereTideEffect : public sphere::RingEffect {
public:
    /// Catalog tags: MoonLight origin.
    const char* tags() const override { return "💫"; }
    /// How fast the swells travel round.
    uint8_t speed = 60;
    /// How narrow the swells are.
    uint8_t reach = 1;
    /// How much is left of them at neap tide.
    uint8_t low = 60;

    /// Publish the speed, the swells' width and the neap level.
    void defineControls() override {
        controls_.addControl("speed", speed, 8, 255);
        controls_.addControl("reach", reach, 0, 3);
        controls_.addControl("low", low, 0, 255);
    }

    /// Draw both swells at this hour of the tide.
    void tick() MM_NONBLOCKING override {
        const uint32_t t = elapsed();
        const uint32_t moonMs = 1560000 / speed;  // ms for the swells to go once round: 26 s at 60
        const uint16_t moon = sphere::phaseOf(t, moonMs);
        const uint16_t sun = sphere::phaseOf(t, moonMs * 4);
        int32_t high = sphere::cosine(static_cast<uint16_t>(moon - sun) * 2) / 256;  // 255 with the two in line, 0 crosswise
        high = low + (255 - low) * high / 255;
        const uint8_t near = sphere::brightIndex(static_cast<uint8_t>(t / 300));
        const uint8_t far = sphere::brightIndex(static_cast<uint8_t>(t / 300 + 128));
        forEachLight([&](uint8_t* out, uint32_t i, uint8_t channels, uint16_t a) {
            const uint16_t off = static_cast<uint16_t>(a - moon);
            int32_t v = sphere::cosine(off * 2) / 256;  // two crests a turn
            v = v * v / 255;
            for (uint8_t s = 0; s < reach; ++s) v = v * v / 255;
            const uint8_t h = off > 16384 && off < 49152 ? far : near;  // the swell on the far side
            sphere::put(out, i, channels, sphere::color(h, v * high / 255));
        });
    }
};

} // namespace mm
