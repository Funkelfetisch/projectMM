#pragma once

#include <cstdint>

namespace mm {

/// Which channels each light's preview colour shows: what the outputs driving that light emit, so an RGB strip shows no white and a white-only accent shows no colour. A light no output drives shows everything, as composed.
struct PreviewOutputMap {
    /// The channels a light emits: its colour, its white, or both.
    static constexpr uint8_t kColour = 1, kWhite = 2;

    /// One output's window over the source buffer, and the channels it emits.
    struct Window {
        /// The first light the output drives.
        uint32_t start = 0;
        /// How many lights it drives.
        uint32_t length = 0;
        /// kColour and kWhite, as its fixture carries them.
        uint8_t channels = 0;
    };
    /// The outputs the map holds; with more, every light shows everything, as with no map.
    static constexpr uint8_t kCapacity = 16;

    /// Forget every output, so every light shows everything.
    void clear() { count_ = 0; overflow_ = false; }
    /// Record one output's window and the channels it emits.
    void add(uint32_t start, uint32_t length, uint8_t channels) {
        if (count_ < kCapacity) windows_[count_++] = {start, length, channels};
        else overflow_ = true;
    }
    /// The channels the outputs driving this light emit together; both for a light no output drives.
    uint8_t channels(uint32_t light) const {
        if (overflow_) return kColour | kWhite;
        bool covered = false;
        uint8_t emitted = 0;
        for (uint8_t i = 0; i < count_; ++i) {
            const Window& w = windows_[i];
            if (light - w.start >= w.length) continue;   // unsigned: a light before the window wraps past its length
            covered = true;
            emitted |= w.channels;
        }
        return covered ? emitted : kColour | kWhite;
    }

private:
    Window windows_[kCapacity];
    uint8_t count_ = 0;
    bool overflow_ = false;
};

} // namespace mm
