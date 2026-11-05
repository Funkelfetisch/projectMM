#pragma once

#include "platform/platform.h"

#include <cstdint>

namespace mm::helio {

/// The NEBULITE S3 battery controller (Rev4) keeps its own power on: its "on" button raises the power switch only while pressed, and the firmware holds it.
///
/// GPIO10 reading high marks a battery controller, whose GPIO21 (enable) and GPIO14 (main enable) then go high, as the pinned NEBULITE WLED source does (docs/work/present/NEBULITE S3 battery controller startup.md). A USB controller reads GPIO10 low and carries a button on GPIO21, so it is left alone.
struct NebuliteS3Power {
    static constexpr uint8_t kProbeGpio = 10, kEnableGpio = 21, kMainEnableGpio = 14;

    /// Drive both enables high on a battery controller and hold them there, so an update's restart, a reboot or a crash keeps the board powered; true when both are held.
    static bool hold() {
        if (!platform::gpioInputBegin(kProbeGpio, platform::GpioPull::None) || !platform::gpioRead(kProbeGpio)) return false;
        bool held = true;
        // Driven before held: the hold latches the level it finds.
        for (const uint8_t gpio : {kEnableGpio, kMainEnableGpio})
            held = platform::gpioWrite(gpio, true) && platform::gpioHold(gpio) && held;
        return held;
    }
};

} // namespace mm::helio
