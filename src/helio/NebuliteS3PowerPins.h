#pragma once

#include <cstdint>

namespace mm::helio {
/// The NEBULITE S3 battery controller's power pins, which MoonLight's power hold and the S3 migration image both drive.
struct NebuliteS3PowerPins {
    /// The battery-controller probe, the enable and the main enable.
    static constexpr uint8_t kProbeGpio = 10, kEnableGpio = 21, kMainEnableGpio = 14;
};
}
