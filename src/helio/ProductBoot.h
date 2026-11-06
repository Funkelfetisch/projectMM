#pragma once

#include "core/system/NetworkModule.h"
#include "core/util/build_info.h"
#include "helio/NebuliteS3Power.h"
#include "platform/platform.h"

#include <cstdint>
#include <cstring>

namespace mm::helio {

/// The classic NEBULITE board's pins that no module claims, settled before any module starts.
struct NebuliteClassicBoard {
    /// The buzzer's NPN base, through 100 Ohm: GPIO5 leaves reset with its weak pull-up on, so the buzzer draws a standing current that warms the board until the pin is driven low.
    static constexpr uint8_t kBuzzerGpio = 5;
    /// A strap pin wired to the TOUCH2 pad, pulled down: an input held low rather than driven.
    static constexpr uint8_t kStrapGpio = 2;

    /// Drive the buzzer pin low and hold the strap pin low with its pull-down.
    static void settle() {
        platform::gpioPreloadLowOutput(kBuzzerGpio);
        platform::gpioInputBegin(kStrapGpio, platform::GpioPull::Down);
    }
};

/// The pins a product firmware settled, which boot reports as they read back.
struct BoardPins {
    /// The settled GPIOs, the first `count` of them.
    uint8_t gpio[3] = {};
    /// How many GPIOs were settled; none for a firmware without board pins.
    uint8_t count = 0;
};

/// Settle the board a product firmware runs on, before any module starts: the classic NEBULITE board's buzzer and strap pins, the S3 battery controller's power hold. Any other firmware leaves its pins alone.
inline BoardPins settleBoard(const char* firmware) {
    if (std::strcmp(firmware, "nebulite-esp32") == 0) {
        NebuliteClassicBoard::settle();
        return {{NebuliteClassicBoard::kBuzzerGpio, NebuliteClassicBoard::kStrapGpio}, 2};
    }
    if (std::strcmp(firmware, "nebulite-s3") == 0) {
        NebuliteS3Power::hold();
        return {{NebuliteS3Power::kProbeGpio, NebuliteS3Power::kEnableGpio, NebuliteS3Power::kMainEnableGpio}, 3};
    }
    return {};
}

/// A garment is worn on a battery and reached by its app over BLE, so it boots with the radio down, and sleeps it between beacons once the owner turns it on for an update or a venue. HELIO stays on the mains and keeps both as they are.
inline void applyNetworkDefaults(NetworkModule& network, const char* firmware) {
    if (!isNebuliteFirmware(firmware)) return;
    network.setWifiDefaultEnabled(false);
    network.setWifiPowerSaveDefault(true);
}

} // namespace mm::helio
