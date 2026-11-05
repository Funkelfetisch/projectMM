/// @module platform

/// The NEBULITE S3 battery controller keeps its own power on through every restart, and a USB controller's pins are left alone.
#include "doctest.h"
#include "helio/NebuliteS3Power.h"

using mm::helio::NebuliteS3Power;
namespace platform = mm::platform;

TEST_CASE("a battery controller drives both power enables high and holds them through a restart") {
    platform::clearTestGpioLevel();
    platform::setTestGpioLevel(NebuliteS3Power::kProbeGpio, true);
    CHECK(NebuliteS3Power::hold());
    CHECK(platform::gpioRead(NebuliteS3Power::kEnableGpio));
    CHECK(platform::gpioRead(NebuliteS3Power::kMainEnableGpio));
    // A held pad keeps its level whatever is written after, which is what carries it through a reset.
    platform::gpioWrite(NebuliteS3Power::kEnableGpio, false);
    platform::gpioWrite(NebuliteS3Power::kMainEnableGpio, false);
    CHECK(platform::gpioRead(NebuliteS3Power::kEnableGpio));
    CHECK(platform::gpioRead(NebuliteS3Power::kMainEnableGpio));
    // The next boot drives and holds them again, still high.
    platform::setTestGpioLevel(NebuliteS3Power::kProbeGpio, true);
    CHECK(NebuliteS3Power::hold());
    CHECK(platform::gpioRead(NebuliteS3Power::kMainEnableGpio));
    platform::clearTestGpioLevel();
}

TEST_CASE("a USB controller's enable pins are neither driven nor held") {
    platform::clearTestGpioLevel();
    CHECK_FALSE(NebuliteS3Power::hold());   // GPIO10 reads low
    CHECK_FALSE(platform::gpioRead(NebuliteS3Power::kEnableGpio));
    // GPIO21 is that board's button, still free to follow its input.
    platform::gpioWrite(NebuliteS3Power::kEnableGpio, true);
    CHECK(platform::gpioRead(NebuliteS3Power::kEnableGpio));
    platform::clearTestGpioLevel();
}
