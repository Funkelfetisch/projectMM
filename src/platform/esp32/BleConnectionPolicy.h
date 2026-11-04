#pragma once
#include <cstdint>

namespace mm::platform {
/// A connection-specific request, not a claim that the central accepts it.
struct BleConnectionHint {
    /// The interval range in 1.25 ms units, the peripheral latency in events, and the supervision timeout in 10 ms units.
    uint16_t minInterval, maxInterval, latency, timeout;
};

/// Ask a valid link for 15 to 30 ms with no latency when the central's is slower or has latency, and for at least four seconds supervision; a faster central keeps its interval, and a longer timeout stays.
inline bool bleConnectionHint(uint16_t interval, uint16_t latency, uint16_t timeout,
                              BleConnectionHint& out) {
    if (interval < 6 || interval > 3200 || latency > 499 || timeout < 10 || timeout > 3200)
        return false;
    // Spec: timeout*10 ms must be strictly greater than twice the largest connection interval times (latency+1); interval is in 1.25 ms units.
    if (static_cast<uint64_t>(timeout) * 8u <= static_cast<uint64_t>(latency + 1u) * interval * 2u)
        return false;
    /// 15 and 30 ms and 4 s, in their units: Apple's Accessory Design Guidelines want a minimum of at least 15 ms and a maximum at least 15 ms above it, so iOS accepts the request.
    constexpr uint16_t kFastMin = 12, kFastMax = 24, kDesiredTimeout = 400;
    const bool slow = interval > kFastMax || latency > 0;
    if (!slow && timeout >= kDesiredTimeout) return false;
    out = {slow ? kFastMin : interval, slow ? kFastMax : interval, 0, timeout > kDesiredTimeout ? timeout : kDesiredTimeout};
    return true;
}
} // namespace mm::platform
