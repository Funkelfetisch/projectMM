#pragma once

#include "light/effects/WledWashingMotion.h"
#include <cstdint>

namespace mm {
/// WLED-MM `simulateSound(0)` volume channel: fftResult[8] is a 13 BPM beatsin8_t. The caller supplies the WLED clock; this does not model audio.
/// @attribution WLED and FastLED contributors
class WledLegacyAudio {
public:
    struct SimulatedPeak {
        /// Whether a beat fires on this draw.
        bool peak;
        /// The beat's major-peak frequency in Hz.
        float frequencyHz;
        /// The beat's magnitude, in WLED's scale.
        float magnitude;
    };

    /// The 13 BPM sine volume at `nowMs`, from 0 to 255.
    static uint8_t volume(uint32_t nowMs) {
        const uint32_t product = nowMs * (13u << 8) * 280u;
        // beatsin8_t(13,0,255) uses fixed scale8(sine,255), i.e. identity.
        return WledWashingMotion::sine(uint8_t(product >> 24));
    }

    /// The caller owns the random stream and supplies one WLED random8 draw; the magnitude stays in WLED's scale, which AudioFrame::peakMag does not share.
    static SimulatedPeak peak(uint8_t volume, uint8_t random) {
        const float level = volume;
        return {random > 250, 21.0f + (level * level) / 8.0f,
                volume < 1 ? 0.001f : 1250.0f};
    }
};
} // namespace mm
