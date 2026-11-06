/// @module platform
/// @also NetworkModule

/// What a product firmware sets before any module starts, through the same calls boot makes: each board's unclaimed pins, and a garment's radio defaults.
#include "doctest.h"
#include "helio/ProductBoot.h"

#include <cstring>

using mm::helio::NebuliteClassicBoard;
namespace platform = mm::platform;

TEST_CASE("a classic NEBULITE firmware holds GPIO2 low with its pull-down and drives the buzzer pin low") {
    platform::clearTestGpioLevel();
    // Both pins left reset high, so only the settle brings them low.
    platform::gpioWrite(NebuliteClassicBoard::kStrapGpio, true);
    platform::gpioWrite(NebuliteClassicBoard::kBuzzerGpio, true);
    const auto pins = mm::helio::settleBoard("nebulite-esp32");
    platform::GpioPull pull = platform::GpioPull::None;
    REQUIRE(platform::testGpioInputPull(NebuliteClassicBoard::kStrapGpio, pull));
    CHECK(pull == platform::GpioPull::Down);
    CHECK_FALSE(platform::gpioRead(NebuliteClassicBoard::kStrapGpio));
    CHECK_FALSE(platform::gpioRead(NebuliteClassicBoard::kBuzzerGpio));
    REQUIRE(pins.count == 2);
    CHECK(pins.gpio[0] == NebuliteClassicBoard::kBuzzerGpio);
    CHECK(pins.gpio[1] == NebuliteClassicBoard::kStrapGpio);
    platform::clearTestGpioLevel();
}

TEST_CASE("an S3 garment and HELIO leave the classic board's buzzer and strap pins alone") {
    for (const char* firmware : {"nebulite-s3", "helio1"}) {
        platform::clearTestGpioLevel();
        platform::gpioWrite(NebuliteClassicBoard::kStrapGpio, true);
        platform::gpioWrite(NebuliteClassicBoard::kBuzzerGpio, true);
        mm::helio::settleBoard(firmware);
        platform::GpioPull pull = platform::GpioPull::None;
        CHECK_FALSE(platform::testGpioInputPull(NebuliteClassicBoard::kStrapGpio, pull));
        CHECK(platform::gpioRead(NebuliteClassicBoard::kStrapGpio));
        CHECK(platform::gpioRead(NebuliteClassicBoard::kBuzzerGpio));
    }
    CHECK(mm::helio::settleBoard("helio1").count == 0);
    platform::clearTestGpioLevel();
}

namespace {
bool wifiDefault(const char* firmware) {
    mm::NetworkModule network;
    network.allowWifiToggle(true);   // as boot does on every product firmware
    mm::helio::applyNetworkDefaults(network, firmware);
    network.defineControls();
    for (uint8_t i = 0; i < network.controls().count(); ++i)
        if (std::strcmp(network.controls()[i].name, "wifiEnabled") == 0) return *static_cast<bool*>(network.controls()[i].ptr);
    FAIL("NetworkModule publishes no wifiEnabled control");
    return true;
}
}  // namespace

TEST_CASE("every NEBULITE garment boots with Wi-Fi off, and HELIO keeps it on") {
    CHECK_FALSE(wifiDefault("nebulite-esp32"));
    CHECK_FALSE(wifiDefault("nebulite-s3"));
    CHECK(wifiDefault("helio1"));
}
