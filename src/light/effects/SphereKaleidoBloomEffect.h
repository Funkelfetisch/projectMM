#pragma once

#include "light/effects/SphereRing.h"

namespace mm {

/// A flower of three to eight petals that opens from the petals' middles until they touch, folds again and comes back with another count.
///
/// One petal's pattern is mirrored in itself and repeated round the ring, so the figure always closes. The counts run 3 4 6 5 8 6 4, each bloom in the next color, with veins crawling along the petals.
class SphereKaleidoBloomEffect : public sphere::RingEffect {
public:
    /// Catalog tags: MoonLight origin.
    const char* tags() const override { return "💫"; }
    /// How fast the flower opens and folds.
    uint8_t speed = 100;
    /// How finely each petal is drawn.
    uint8_t veins = 3;
    /// How fast the flower turns.
    uint8_t drift = 30;

    /// Publish the bloom speed, the veins and the drift.
    void defineControls() override {
        controls_.addControl("speed", speed, 1, 255);
        controls_.addControl("veins", veins, 0, 8);
        controls_.addControl("drift", drift, 0, 255);
    }

    /// The petal count of each bloom in a cycle of seven.
    static constexpr uint8_t kCounts[7] = {3, 4, 6, 5, 8, 6, 4};

    /// Draw this bloom's petals as far as they are open.
    void tick() MM_NONBLOCKING override {
        const uint32_t t = elapsed();
        const uint32_t bloomMs = 9000 - speed * 22;  // one opening and folding
        const uint32_t n = (t / bloomMs) % 252;
        const int32_t count = kCounts[n % 7];
        int32_t open = (65535 - sphere::cosine(static_cast<int32_t>((t % bloomMs) * 65536 / bloomMs))) / 2;  // 0 folded, 32767 open, 0 again
        open = std::min<int32_t>(open * 3, 65535);  // and open a while before it folds
        const uint16_t spin = drift ? sphere::phaseOf(t, std::min<uint32_t>(4000000u / drift, 260000u)) : 0;
        const int32_t crawl = static_cast<int32_t>(t % 8192) * 8;
        const uint8_t near = sphere::brightIndex(static_cast<uint8_t>(n * 37));
        forEachLight([&](uint8_t* out, uint32_t i, uint8_t channels, uint16_t a) {
            // Where in its petal this place lies: 0 at the petal's edge, 65534 at its middle, mirrored.
            int32_t p = static_cast<int32_t>(static_cast<uint16_t>(static_cast<uint16_t>(a + spin) * count)) * 2;
            if (p > 65534) p = 131070 - p;
            int32_t bri = 0;
            if (p > 65535 - open) {  // a petal shows where it is nearer its middle than its opening allows
                bri = (p - 65535 + open) / 64 * 255 / (open / 64 + 1);
                bri = std::min<int32_t>(bri * 2, 255);
                const int32_t vein = sphere::sine(p * veins / 2 - crawl) / 624;
                if (veins > 0) bri = bri * (150 + vein) / 255;
            }
            const uint8_t h = static_cast<uint8_t>(near + p / 2800);  // the petal's middle lies a little further along the palette
            sphere::put(out, i, channels, sphere::color(h, bri));
        });
    }
};

} // namespace mm
