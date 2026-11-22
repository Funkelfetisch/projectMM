#pragma once

#include "light/effects/SphereRing.h"

namespace mm {

/// The ring's own notes: two to six standing waves that fit round it exactly, swelling and sinking like a plucked string, each crest returning in the other color.
///
/// An odd count puts every crest across from a trough, so each color faces the other through the sphere; an even count faces it with itself. The count runs 2 3 4 5 6 5 4 3 and changes at the dark instant between two swells, where a new pair of colors half the palette apart takes over.
class SphereHarmonicsEffect : public sphere::RingEffect {
public:
    /// Catalog tags: MoonLight origin.
    const char* tags() const override { return "💫"; }
    /// How fast the waves swell and sink.
    uint8_t speed = 100;
    /// How fast the whole figure turns.
    uint8_t drift = 40;
    /// How narrow the crests are.
    uint8_t sharp = 1;

    /// Publish the swell speed, the drift and the crests' sharpness.
    void defineControls() override {
        controls_.addControl("speed", speed, 1, 255);
        controls_.addControl("drift", drift, 0, 255);
        controls_.addControl("sharp", sharp, 0, 3);
    }

    /// Draw the standing waves of this swell.
    void tick() MM_NONBLOCKING override {
        const uint32_t t = elapsed();
        const uint32_t beatMs = 5200 - speed * 16;  // one swell, dark to dark
        const uint32_t n = (t / beatMs) % 8;
        const int32_t waves = n > 4 ? 10 - static_cast<int32_t>(n) : 2 + static_cast<int32_t>(n);
        const int32_t swell = sphere::sine(static_cast<int32_t>((t % (beatMs * 2)) * 32768 / beatMs)) - 32768;  // zero when the count changes
        const uint16_t spin = drift ? sphere::phaseOf(t, std::min<uint32_t>(2600000u / drift, 260000u)) : 0;
        const uint8_t hue = static_cast<uint8_t>((t / beatMs) % 256 * 29);  // a new pair of colors with every swell
        const uint8_t near = sphere::brightIndex(hue);
        const uint8_t far = sphere::brightIndex(static_cast<uint8_t>(hue + 128));
        forEachLight([&](uint8_t* out, uint32_t i, uint8_t channels, uint16_t a) {
            const int32_t along = static_cast<int32_t>(static_cast<uint16_t>(a + spin)) * waves;
            int32_t v = ((sphere::sine(along) - 32768) / 256) * (swell / 256);  // -16384..16384
            uint8_t h = near;
            if (v < 0) { v = -v; h = far; }  // the same wave, swung through to the other side
            int32_t bri = std::min<int32_t>(v / 64, 255);
            for (uint8_t s = 0; s < sharp; ++s) bri = bri * bri / 255;
            sphere::put(out, i, channels, sphere::color(h, bri));
        });
    }
};

} // namespace mm
