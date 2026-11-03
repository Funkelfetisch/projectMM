#pragma once

#include "core/util/ActiveInstance.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>

namespace mm {
/// Session-only monotonic countdown and temporary physical-order segmented display.
///
/// The output stage reads this after compositing, so presets and effect buffers remain intact.
class RemoteTimer {
public:
    ActiveInstance<RemoteTimer> seat{*this};
    /// Milliseconds left on the countdown; zero when idle.
    uint64_t remainingMs = 0;
    /// Count the time since the last call down and return whether the countdown just ran out.
    bool advance(uint32_t now) {
        const uint32_t elapsed = sampled_ ? now - lastMs_ : 0;
        sampled_ = true; lastMs_ = now;
        const bool expired = remainingMs && elapsed >= remainingMs;
        remainingMs = elapsed >= remainingMs ? 0 : remainingMs - elapsed;
        confirmedMs_ = elapsed >= confirmedMs_ ? 0 : confirmedMs_ - elapsed;
        return expired;
    }
    /// Add thirty minutes and show the display; false when the total would overflow.
    bool add(uint32_t now) {
        advance(now);
        constexpr uint64_t increment = 30u * 60u * 1000u;
        if (remainingMs > UINT64_MAX - increment) return false;
        if (!confirming(now)) confirmedMs_ = remainingMs;
        remainingMs += increment; pressedAt_ = now; showing_ = true; indicating_ = false;
        return true;
    }
    /// Add thirty minutes, or cancel with a black indication when that passes `maximumMinutes` (zero means no limit).
    bool cycle(uint32_t now, uint16_t maximumMinutes) {
        advance(now);
        if (maximumMinutes && minutes() + 30 > maximumMinutes) {
            cancel(); indicate(now, 0, 0, 0, 0);
            return true;
        }
        return add(now);
    }
    /// Handle a button press: reveal a hidden running countdown, else cycle it.
    bool press(uint32_t now, uint16_t maximumMinutes) {
        advance(now);
        if (remainingMs && !visible(now)) {
            confirmedMs_ = remainingMs;
            pressedAt_ = now; showing_ = true; indicating_ = false;
            return true;
        }
        return cycle(now, maximumMinutes);
    }
    /// Stop the countdown and hide the display.
    void cancel() { remainingMs = confirmedMs_ = 0; showing_ = indicating_ = false; }
    /// Show one solid red, green, blue and white color across every light for 700 ms, overriding the countdown display.
    void indicate(uint32_t now, uint8_t red, uint8_t green, uint8_t blue, uint8_t white) {
        indication_[0] = red; indication_[1] = green; indication_[2] = blue; indication_[3] = white;
        indicatedAt_ = now; indicating_ = true;
    }
    /// Whether an indication is still showing.
    bool indicating(uint32_t now) const { return indicating_ && now - indicatedAt_ < 700; }
    /// The time left in minutes, rounded up.
    uint64_t minutes() const { return remainingMs / 60000 + (remainingMs % 60000 != 0); }
    /// Whether the countdown display shows: for four seconds after a press while time remains.
    bool visible(uint32_t now) const { return showing_ && remainingMs && now - pressedAt_ < 4000; }
    /// Whether the display is inside the three-second window where time just added blinks.
    bool confirming(uint32_t now) const { return visible(now) && now - pressedAt_ < 3000; }
    /// Write the indication or the countdown display into `bytes` and return whether it drew; false leaves the buffer untouched.
    bool render(std::span<uint8_t> bytes, uint8_t channels, uint32_t now) {
        if (channels < 3 || bytes.size() < channels) return false;
        const size_t count = bytes.size() / channels;
        if (indicating(now)) {
            std::memset(bytes.data(), 0, bytes.size());
            for (size_t i = 0; i < count; ++i)
                for (uint8_t c = 0; c < std::min(uint8_t(4), channels); ++c)
                    bytes[i * channels + c] = channels == 3 ? std::max(indication_[c], indication_[3]) : indication_[c];
            return true;
        }
        if (!visible(now)) return false;
        constexpr uint64_t fullDuration = 180u * 60000u;
        const uint64_t remaining = std::min(remainingMs, fullDuration);
        const uint64_t confirmed = std::min(confirmedMs_, fullDuration);
        const size_t litLights = (remaining * count + fullDuration - 1) / fullDuration;
        const size_t confirmedLights = (confirmed * count + fullDuration - 1) / fullDuration;
        const bool blinkOn = ((now - pressedAt_) / 250) % 2 == 0;
        std::memset(bytes.data(), 0, bytes.size());
        for (size_t i = 0; i < litLights; ++i) {
            if (confirming(now) && i >= confirmedLights && !blinkOn) continue;
            const size_t segment = i * 18 / count;
            bytes[i * channels] = 255 - segment * 255 / 17;
            bytes[i * channels + 1] = segment * 255 / 17;
        }
        return true;
    }
private:
    uint8_t indication_[4] = {};
    uint32_t indicatedAt_ = 0;
    bool indicating_ = false;
    uint64_t confirmedMs_ = 0;
    uint32_t lastMs_ = 0, pressedAt_ = 0;
    bool sampled_ = false, showing_ = false;
};
} // namespace mm
