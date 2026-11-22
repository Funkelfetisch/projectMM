#pragma once

#include "core/services/AudioService.h"
#include "core/util/math16.h"
#include "core/util/noise.h"
#include "light/effects/EffectBase.h"
#include "light/util/Palette.h"

#include <algorithm>

namespace mm::sphere {

/// The angle of light `i` of `count` round the ring.
inline uint16_t angleOf(uint32_t i, uint32_t count) {
    return static_cast<uint16_t>((static_cast<uint64_t>(i) << 16) / count);
}

/// How far `t` has come through a period of `periodMs`, as an angle.
inline uint16_t phaseOf(uint32_t t, uint32_t periodMs) {
    return static_cast<uint16_t>((static_cast<uint64_t>(t % periodMs) << 16) / periodMs);
}

/// The sine of an angle on 0..65535, centered on 32768, the range the scripts these effects came from read.
inline int32_t sine(int32_t angle) { return sin16(static_cast<angle16>(angle)) + 32768; }
/// The cosine on the same range.
inline int32_t cosine(int32_t angle) { return cos16(static_cast<angle16>(angle)) + 32768; }

/// A level clamped into a byte, since the sums below may run past either end.
inline uint8_t level(int32_t v) { return v < 0 ? 0 : (v > 255 ? 255 : static_cast<uint8_t>(v)); }

/// The active palette's color at `index`, at `brightness`.
inline RGB color(uint8_t index, int32_t brightness) {
    return colorFromPalette(*Palettes::active(), index, level(brightness));
}

/// The first index from `start` on, in eighths of the palette, whose color is not dark: many palettes keep a dark stretch, and a light painted from it would vanish.
inline uint8_t brightIndex(uint8_t start) {
    for (uint8_t step = 0; step < 8; ++step) {
        const uint8_t index = static_cast<uint8_t>(start + step * 32);
        const RGB c = color(index, 255);
        if (c.r > 110 || c.g > 110 || c.b > 110) return index;
    }
    return start;
}

/// Whether the room is loud and the level jumped above its average, the same beat the scripts' `audioBeat` reports.
inline bool onBeat() {
    const AudioFrame* a = AudioService::latestFrame();
    constexpr uint16_t kSilence = 8, kBeatMargin = 8;
    return a->levelSmoothed >= kSilence && a->level > a->levelSmoothed + kBeatMargin;
}

/// How far a place lies from the line through the sphere at `axis`, 0 at its ends and 16384 crosswise: a pattern on it lands every reflection inside the ball on its own image.
inline int32_t fromAxis(uint16_t a, uint16_t axis) {
    const int32_t f = static_cast<uint16_t>(a - axis) % 32768;
    return f > 16384 ? 32768 - f : f;
}

/// The mean of the smoothed bands `from` to `to`, inclusive, 0..255: the music without the hard edges of each hit.
inline int32_t bands(uint8_t from, uint8_t to) {
    const AudioFrame* audio = AudioService::latestFrame();
    int32_t sum = 0;
    for (uint8_t i = from; i <= to; ++i) sum += audio->bandsSmoothed[i];
    return sum / (to - from + 1);
}

/// A level that rises and falls at set rates, so the light follows the music without blinking.
///
/// It keeps eight fraction bits; `rise` and `fall` are how far it closes the gap per 10 ms, out of 256.
struct Envelope {
    int32_t value = 0;  ///< the level, 0..255 with eight fraction bits
    /// Move toward `target` over `dt` ms.
    void step(int32_t target, int32_t dt, int32_t rise, int32_t fall) {
        const int32_t goal = std::clamp<int32_t>(target, 0, 255) << 8;
        const int32_t rate = std::min<int32_t>((goal > value ? rise : fall) * dt / 10, 256);
        value += (goal - value) * rate / 256;
    }
    /// The level, 0..255.
    int32_t level() const { return value >> 8; }
};

/// Write one light, its white channel dark: the effects mix their whites out of RGB.
inline void put(uint8_t* out, uint32_t i, uint8_t channels, int32_t r, int32_t g, int32_t b) {
    uint8_t* p = out + static_cast<size_t>(i) * channels;
    p[0] = level(r); p[1] = level(g); p[2] = level(b);
    if (channels > 3) p[3] = 0;
}
inline void put(uint8_t* out, uint32_t i, uint8_t channels, RGB c) { put(out, i, channels, c.r, c.g, c.b); }

/// The common shape of a sphere effect: the strip read as a closed ring, painted by angle.
///
/// A light's place is its angle, 0..65535 once round, so the place straight across is half a turn on at any light count. Inside a ball every reflection lands on the opposite side, which is what the sphere effects build on: lights in opposite pairs, waves that meet straight across, colors that face their opposites. Angles wrap as `uint16_t`, the binary angle.
class RingEffect : public EffectBase {
public:
    /// Declares the full 3D set; the render indexes lights directly.
    Dim dimensions() const override { return Dim::D3; }
    /// The ring is the strip as it is wired, so the angle follows the wiring.
    bool usesPhysicalLightOrder() const override { return true; }

    /// Hand every light to `paint(i, angle)` once the frame holds a ring to draw.
    template<typename Paint>
    void forEachLight(Paint&& paint) {
        uint8_t* out = buffer();
        const uint32_t count = nrOfLights();
        const uint8_t channels = channelsPerLight();
        if (!out || channels < 3 || !count) return;
        for (uint32_t i = 0; i < count; ++i) paint(out, i, channels, angleOf(i, count));
    }
};

} // namespace mm::sphere
