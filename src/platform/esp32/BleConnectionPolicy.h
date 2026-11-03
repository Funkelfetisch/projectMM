#pragma once
#include <cstdint>

namespace mm::platform {
/// A connection-specific request, not a claim that the central accepts it.
struct BleConnectionHint {
    /// The connection interval in 1.25 ms units, the peripheral latency in events, and the supervision timeout in 10 ms units.
    uint16_t interval, latency, timeout;
};

/// Request at least four seconds supervision only for a valid shorter link, keeping the central's interval and latency and never shortening its timeout.
inline bool bleConnectionHint(uint16_t interval, uint16_t latency, uint16_t timeout,
                              BleConnectionHint& out) {
    if (interval < 6 || interval > 3200 || latency > 499 || timeout < 10 || timeout > 3200)
        return false;
    // Spec: timeout*10 ms must be strictly greater than twice the largest connection interval times (latency+1); interval is in 1.25 ms units.
    if (static_cast<uint64_t>(timeout) * 8u <= static_cast<uint64_t>(latency + 1u) * interval * 2u)
        return false;
    constexpr uint16_t kDesiredTimeout = 400; // 4 s, in 10 ms units
    if (timeout >= kDesiredTimeout) return false;
    out = {interval, latency, kDesiredTimeout};
    return true;
}
} // namespace mm::platform
