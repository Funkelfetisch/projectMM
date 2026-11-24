// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <cstring>

/// Why a device sits in safe mode, and the current limit that would keep its supply up.
///
/// A supply that gives out reads as a brownout, or as a power-on when it cuts the chip off entirely. Both count as power. The LED draw the failed boot reached, estimated as `maxCurrent` estimates it, places the limit below the point where the supply gave out.

namespace mm {

/// The cause of a safe mode and what it suggests.
struct SafeModeDiagnosis {
    bool power = false;                ///< the supply gave out, rather than the firmware crashing
    uint16_t drawMa = 0;               ///< the estimated LED draw the failed boot had reached, 0 when unknown
    uint16_t suggestedMaxCurrent = 0;  ///< a `maxCurrent` under that draw, 0 when no LED limit helps
};

/// Below this draw the LEDs are not what overloads the supply, so a limit would not help.
constexpr uint16_t kSafeModeMinLimitMa = 200;

/// Diagnose from how the failed boot ended and the draw it had reached: the limit is 80 % of that draw, in 50 mA steps.
inline SafeModeDiagnosis diagnoseSafeMode(const char* resetReason, uint16_t failedDrawMa) {
    SafeModeDiagnosis d;
    d.power = resetReason && (std::strcmp(resetReason, "POWERON") == 0 || std::strcmp(resetReason, "BROWNOUT") == 0);
    d.drawMa = failedDrawMa;
    const uint32_t limit = uint32_t{failedDrawMa} * 8 / 10 / 50 * 50;
    if (d.power && limit >= kSafeModeMinLimitMa) d.suggestedMaxCurrent = static_cast<uint16_t>(limit);
    return d;
}

}  // namespace mm
