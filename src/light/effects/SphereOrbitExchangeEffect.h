#pragma once

#include "light/effects/SphereRing.h"

namespace mm {

/// Two lights circle the ring against each other, flash where they pass and trade colors; the far side of the sphere answers each flash.
///
/// They pass twice a turn, at two places straight across from each other, and the passing place drifts slowly round, once in four minutes.
class SphereOrbitExchangeEffect : public sphere::RingEffect {
public:
    /// Catalog tags: MoonLight origin.
    const char* tags() const override { return "💫"; }
    /// How fast the two lights circle.
    uint8_t speed = 120;
    /// How long their tails are.
    uint8_t tail = 110;
    /// How bright the flash is where they pass.
    uint8_t flash = 200;

    /// Publish the speed, the tails and the flash.
    void defineControls() override {
        controls_.addControl("speed", speed, 1, 255);
        controls_.addControl("tail", tail, 5, 200);
        controls_.addControl("flash", flash, 0, 255);
    }

    /// Draw both lights from their last passing, and the flash it left.
    void tick() MM_NONBLOCKING override {
        const uint32_t t = elapsed();
        const uint32_t passMs = 5200 - speed * 18;  // ms from one passing to the next
        const uint32_t n = (t / passMs) % 256;
        const int32_t since = static_cast<int32_t>((t % passMs) * 32768 / passMs);  // how far each has come since they passed
        const bool odd = n % 2 == 1;
        const uint16_t meet = static_cast<uint16_t>(sphere::phaseOf(t, 240000) + (odd ? 32768 : 0));
        const auto hueOf = [](uint32_t pass) { return static_cast<uint8_t>(pass * 23 + (pass % 2 ? 128 : 0)); };  // the trade: each light takes the other's color
        const int32_t len = tail * 100;
        const uint8_t near = sphere::brightIndex(hueOf(n)), nearBefore = sphere::brightIndex(hueOf(n + 255));
        const uint8_t far = sphere::brightIndex(static_cast<uint8_t>(hueOf(n) + 128)), farBefore = sphere::brightIndex(static_cast<uint8_t>(hueOf(n + 255) + 128));
        forEachLight([&](uint8_t* out, uint32_t i, uint8_t channels, uint16_t a) {
            // One light runs on from the passing place, the other runs back from it. Its tail keeps the color it had where it lay before they passed, so nothing vanishes at the passing.
            const auto tailOf = [&](int32_t behind, uint8_t now, uint8_t before) {
                const int32_t v = behind < len ? 255 - behind * 255 / len : 0;
                return sphere::color(behind <= since ? now : before, v * v / 255);
            };
            const RGB one = tailOf(static_cast<uint16_t>(meet + since - a), near, nearBefore);
            const RGB two = tailOf(static_cast<uint16_t>(a - meet + since), far, farBefore);
            int32_t rr = one.r + two.r, gg = one.g + two.g, bb = one.b + two.b;
            if (since < 9000) {
                // The flash where they passed, and a third of it straight across.
                int32_t d = static_cast<uint16_t>(a - meet) % 32768;
                if (d > 16384) d = 32768 - d;
                if (d < 900 + since) {
                    int32_t v = (255 - d * 255 / (900 + since)) * (9000 - since) / 9000 * flash / 255;
                    if (static_cast<uint16_t>(a - meet + 16384) >= 32768) v /= 3;
                    rr += v; gg += v; bb += v;
                }
            }
            sphere::put(out, i, channels, rr, gg, bb);
        });
    }
};

} // namespace mm
