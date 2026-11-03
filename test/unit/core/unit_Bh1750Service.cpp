#include "doctest.h"
#include "core/services/Bh1750Service.h"
#include "light/drivers/Drivers.h"

using namespace mm;

TEST_CASE("BH1750 raw counts and calibration map to brightness") {
    CHECK(Bh1750Service::rawToLux(120) == 100);
    CHECK(Bh1750Service::rawToLux(1200) == 1000);
    CHECK(Bh1750Service::brightnessForLux(250, 0, 500, 16, 255) == 136);
    CHECK(Bh1750Service::brightnessForLux(250, 500, 0, 255, 16) == 136);
    CHECK(Bh1750Service::brightnessForLux(900, 0, 500, 16, 255) == 255);
}

TEST_CASE("ambient brightness falls back to the stored manual value") {
    Drivers drivers;
    drivers.brightness = 77;
    CHECK(drivers.effectiveBrightness() == 77);
    {
        Bh1750Service sensor;
        sensor.injectLuxForTest(250);
        CHECK(drivers.effectiveBrightness() == 136);
        sensor.automatic = false;
        CHECK(drivers.effectiveBrightness() == 77);
        sensor.automatic = true;
        sensor.setEnabled(false);
        CHECK(drivers.effectiveBrightness() == 77);
    }
    CHECK(drivers.effectiveBrightness() == 77);
}
