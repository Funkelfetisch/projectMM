/// @module Bh1750Service
/// @also Drivers
#include "doctest.h"
#include "core/services/Bh1750Service.h"
#include "light/drivers/Drivers.h"

#include <cstring>
#include <string>

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

TEST_CASE("a Drivers built and dropped beside the live one, as a scene's metadata read does, leaves the lights following the ambient sensor") {
    struct Light : DriverBase { void setSourceBuffer(Buffer*) override {} };
    Drivers drivers;
    auto* light = new Light();
    drivers.addChild(light);
    drivers.brightness = 255;
    drivers.defineControls(); drivers.setup(); drivers.prepare();
    REQUIRE(Drivers::active() == &drivers);
    CHECK(light->correction().briLut[255] == 255);
    { Drivers probe; probe.defineControls(); }
    Bh1750Service sensor;
    sensor.injectLuxForTest(250);   // 136 on the default curve, which the hook bakes into every driver's table
    CHECK(light->correction().briLut[255] < 255);
    drivers.release();
}

TEST_CASE("the automatic target and the status read as a percentage, the level people see") {
    Bh1750Service sensor;
    sensor.defineControls();
    sensor.injectLuxForTest(250);   // 136 of 255 on the default curve
    const char* target = nullptr;
    for (uint8_t i = 0; i < sensor.controls().count(); ++i)
        if (std::strcmp(sensor.controls()[i].name, "target") == 0) target = static_cast<const char*>(sensor.controls()[i].ptr);
    REQUIRE(target != nullptr);
    CHECK(std::string(target) == "53%");
    CHECK(std::string(sensor.status()).find("brightness 53%") != std::string::npos);
}
