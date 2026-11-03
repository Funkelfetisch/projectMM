#pragma once

#include <cstdint>

namespace mm {

/// Motion kernel from the archived WLED Washing Machine effect (FX 113). One call corresponds to one rendered WLED frame, not one millisecond. This is only the phase and wave calculation; the original random palette still needs to be ported before shipping the effect.
/// @attribution WLED contributors, Stefan Seegel, DedeHai
class WledWashingMotion {
public:
    /// The drum's velocity at cycle position `x`: a ramp up, a hold, a ramp down and a rest, negated in the second half.
    static int8_t velocity(uint8_t x) {
        int16_t amplitude = 127;
        if (x > 127) { amplitude = -127; x = static_cast<uint8_t>(x - 127); }
        constexpr int pulse = 90, ramp = 15;
        if (x < ramp) return static_cast<int8_t>(int16_t(x) * amplitude / ramp);
        if (x < pulse - ramp) return static_cast<int8_t>(amplitude);
        if (x < pulse) return static_cast<int8_t>(int16_t(pulse - x) * amplitude / ramp);
        return 0;
    }

    /// The phase after one rendered frame at `now`, a higher `speed` taking a bigger step.
    static uint32_t advance(uint32_t phase, uint32_t now, uint8_t speed) {
        const int32_t delta = int32_t(velocity(static_cast<uint8_t>(now >> 7))) * 2048 / (512 - speed);
        return phase + static_cast<uint32_t>(delta); // WLED's uint32 phase wraps.
    }

    /// The sine input for light `index` of `count`: `intensity` sets how many waves span the strip and `phase` moves them.
    static uint8_t waveInput(uint32_t phase, uint32_t index, uint32_t count, uint8_t intensity) {
        if (!count) return 0;
        const uint32_t spatial = (uint32_t(intensity / 25) + 1) * 255u * index / count;
        return static_cast<uint8_t>(spatial + (phase >> 7));
    }

    /// Archived WLED v0.9 sin8_t: 16-bit Bhaskara approximation, rounded to a byte.
    static constexpr uint8_t sine(uint8_t angle) {
        uint32_t theta = uint32_t(angle) * 257u;
        int32_t sign = 1;
        if (theta > 0x7fffu) { theta = 0xffffu - theta; sign = -1; }
        const uint32_t product = theta * (0x7fffu - theta);
        const uint64_t numerator = uint64_t(product) * (4u * 0x7fffu);
        const int32_t denominator = 1342095361 - product;
        const int32_t sample = int32_t(numerator / denominator) * sign + 0x7fff + 128;
        return uint8_t((sample > 0xffff ? 0xffff : sample) >> 8);
    }
};
} // namespace mm
