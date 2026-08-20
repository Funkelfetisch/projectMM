// @module Bh1750Service
// @also Drivers

#include "doctest.h"
#include "core/Bh1750Service.h"
#include "core/Scheduler.h"
#include "core/Services.h"
#include "light/drivers/Drivers.h"

#include <chrono>

using namespace mm;

namespace {

struct Bh1750Rig {
    Scheduler scheduler;
    Services* services = new Services();
    Bh1750Service* sensor = new Bh1750Service();
    Drivers* drivers = new Drivers();

    Bh1750Rig() {
        services->setName("Services");
        sensor->setName("Ambient Light");
        drivers->setName("Drivers");
        drivers->brightness = 77;
        services->addChild(sensor);
        scheduler.addModule(services);
        scheduler.addModule(drivers);
        scheduler.setup();
    }

    ~Bh1750Rig() { scheduler.release(); }

    void set(const char* control, int value) {
        char json[32];
        std::snprintf(json, sizeof(json), "{\"value\":%d}", value);
        REQUIRE(scheduler.setControl("Ambient Light", control, json) ==
                Scheduler::SetControlResult::Ok);
    }
};

} // namespace

TEST_CASE("Bh1750Service keeps blocking I2C work off the scheduler tick") {
    struct DelayGuard {
        DelayGuard() { platform::setTestI2cDelayMs(200); }
        ~DelayGuard() { platform::setTestI2cDelayMs(0); }
    } delay;

    Bh1750Service sensor;
    sensor.defineControls();
    sensor.prepare();

    const auto start = std::chrono::steady_clock::now();
    sensor.tick1s();
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    CHECK(elapsed.count() < 50);
    sensor.release();
}

TEST_CASE("Bh1750Service converts the datasheet raw count to lux") {
    CHECK(Bh1750Service::rawToLux(0) == 0);
    CHECK(Bh1750Service::rawToLux(120) == 100);
    CHECK(Bh1750Service::rawToLux(1200) == 1000);
    CHECK(Bh1750Service::rawToLux(65535) == 54613);
}

TEST_CASE("Bh1750Service maps and clamps both brightness directions") {
    CHECK(Bh1750Service::brightnessForLux(0, 0, 500, 16, 255) == 16);
    CHECK(Bh1750Service::brightnessForLux(250, 0, 500, 16, 255) == 136);
    CHECK(Bh1750Service::brightnessForLux(900, 0, 500, 16, 255) == 255);

    CHECK(Bh1750Service::brightnessForLux(0, 0, 500, 240, 20) == 240);
    CHECK(Bh1750Service::brightnessForLux(250, 0, 500, 240, 20) == 130);
    CHECK(Bh1750Service::brightnessForLux(900, 0, 500, 240, 20) == 20);
}

TEST_CASE("Bh1750Service accepts a new target only at the lux hysteresis") {
    Bh1750Rig rig;
    rig.set("hysteresisLux", 5);
    rig.sensor->injectLuxForTest(100);
    CHECK(rig.sensor->latestLux() == 100);
    CHECK(rig.sensor->acceptedLux() == 100);
    CHECK(rig.sensor->targetBrightness() == 64);
    CHECK(rig.drivers->effectiveBrightness() == 64);

    rig.sensor->injectLuxForTest(104);
    CHECK(rig.sensor->latestLux() == 104);
    CHECK(rig.sensor->acceptedLux() == 100);
    CHECK(rig.drivers->effectiveBrightness() == 64);

    rig.sensor->injectLuxForTest(105);
    CHECK(rig.sensor->acceptedLux() == 105);
    CHECK(rig.sensor->targetBrightness() == 66);
    CHECK(rig.drivers->effectiveBrightness() == 66);
}

TEST_CASE("Bh1750Service calibration changes apply to the accepted reading") {
    Bh1750Rig rig;
    rig.sensor->injectLuxForTest(250);
    CHECK(rig.drivers->effectiveBrightness() == 136);

    rig.set("darkBrightness", 240);
    rig.set("brightBrightness", 20);
    CHECK(rig.sensor->targetBrightness() == 130);
    CHECK(rig.drivers->effectiveBrightness() == 130);
}

TEST_CASE("Bh1750Service automatic off and disable restore manual brightness") {
    Bh1750Rig rig;
    rig.sensor->injectLuxForTest(250);
    CHECK(rig.drivers->effectiveBrightness() == 136);

    rig.set("automatic", 0);
    CHECK(rig.drivers->effectiveBrightness() == 77);

    rig.set("automatic", 1);
    CHECK(rig.drivers->effectiveBrightness() == 136);

    REQUIRE(rig.scheduler.setControl("Ambient Light", "enabled", "{\"value\":false}") ==
            Scheduler::SetControlResult::Ok);
    CHECK(rig.drivers->effectiveBrightness() == 77);
}

TEST_CASE("Bh1750Service without a valid reading leaves manual brightness active") {
    Bh1750Rig rig;
    CHECK_FALSE(rig.sensor->hasReading());
    CHECK(rig.drivers->effectiveBrightness() == 77);
}
