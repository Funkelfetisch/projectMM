#pragma once

#include "light/effects/SphereRing.h"

namespace mm {

/// A line through the middle of the sphere: two opposite lights turn as one, swaying like a compass needle, with tails as long as the line is fast.
///
/// Each end's reflections land by the other, so the pair reads as one line across the ball. The turning swells, slows and runs back a little once in 16 seconds, and the tails' end follows the line and catches up when it slows. The two ends take colors half the palette apart.
class SphereMeridianEffect : public sphere::RingEffect {
public:
    /// Catalog tags: MoonLight origin.
    const char* tags() const override { return "💫"; }
    /// How fast the line turns.
    uint8_t speed = 60;
    /// How long the tails are.
    uint8_t tail = 60;
    /// How wide each end of the line is.
    uint8_t size = 16;

    /// Publish the turning speed, the tails and the size of the ends.
    void defineControls() override {
        controls_.addControl("speed", speed, 1, 255);
        controls_.addControl("tail", tail, 1, 255);
        controls_.addControl("size", size, 2, 60);
    }

    /// Step the line and its tails, then draw both ends.
    void tick() MM_NONBLOCKING override {
        const uint32_t t = elapsed();
        step(t);
        const uint8_t near = sphere::brightIndex(static_cast<uint8_t>(t / 128));
        const uint8_t far = sphere::brightIndex(static_cast<uint8_t>(t / 128 + 128));
        const int32_t reach = size * 100;
        forEachLight([&](uint8_t* out, uint32_t i, uint8_t channels, uint16_t a) {
            int32_t s = static_cast<uint16_t>(a - place_);  // how far ahead of the line's end this place is
            uint8_t h = near;
            if (s >= 16384 && s < 49152) { s -= 32768; h = far; }  // the far end, in the other color
            if (s >= 49152) s -= 65536;
            int32_t bri = 0;
            const int32_t distance = s < 0 ? -s : s;
            if (distance < reach) bri = 255 - distance * 255 / reach;
            if (lag_ < 0 && s <= 0 && s > lag_) bri = std::max(bri, 255 - s * 255 / lag_);
            if (lag_ > 0 && s >= 0 && s < lag_) bri = std::max(bri, 255 - s * 255 / lag_);
            sphere::put(out, i, channels, sphere::color(h, bri));
        });
    }

private:
    /// Move the line in steps of 8 ms and more, so the sums keep their precision at any frame rate.
    void step(uint32_t t) {
        int32_t dt = static_cast<int32_t>(t - last_);
        if (dt >= 0 && dt < 8) return;
        if (dt > 60 || dt < 0) dt = 60;
        last_ = t;
        const int32_t sway = (sphere::sine(static_cast<int32_t>(t % 16384) * 4) - 32768) / 64;  // forward, slowing, back a little
        const int32_t moved = dt * (sway + 160) * speed / 1536;
        place_ = static_cast<uint16_t>(place_ + moved);
        lag_ = std::clamp(lag_ - moved, int32_t(-14000), int32_t(14000));
        lag_ -= lag_ * dt / (tail * 15 + 100);
    }

    uint16_t place_ = 0;  ///< where the line points
    int32_t lag_ = 0;     ///< where the tail ends, measured from the line: behind it when negative
    uint32_t last_ = 0;   ///< when the last step was taken, in ms
};

} // namespace mm
