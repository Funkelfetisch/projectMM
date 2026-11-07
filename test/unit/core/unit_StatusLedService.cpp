/// @module StatusLedService
/// @also Scheduler

/// What the status LED shows for each way a device is reached, the blink each state gives, and the PWM it drives.

#include "doctest.h"
#include "core/services/StatusLedService.h"
#include "core/module/Scheduler.h"
#include "core/module/MoonModule.h"
#include "core/system/FirmwareUpdateModule.h"
#include "platform/platform.h"

#include <cstdio>
#include <cstring>

using namespace mm;

namespace {

// Stands in for the BLE service: only its `connection` readout, which the status LED reads.
struct FakeBle : public MoonModule {
    char connection[16] = "advertising";
    void defineControls() override { controls_.addReadOnly("connection", connection, sizeof(connection)); }
};

// No phone, no network and no install, so each test starts from waiting.
struct Rig {
    Scheduler scheduler;
    FakeBle* ble = new FakeBle();
    StatusLedService* led = new StatusLedService();
    Rig() {
        const uint8_t none[4] = {};
        platform::setTestWifiStaIPv4(none);
        platform::setTestWifiApAvailable(false);
        platform::setTestOtaBusy(false);
        std::snprintf(g_otaStatus, sizeof(g_otaStatus), "%s", "idle");
        ble->setName("BLE");
        led->setName("Status LED");
        scheduler.addModule(ble);
        scheduler.addModule(led);
        scheduler.setup();
    }
    StatusLedService::Showing showing() { return StatusLedService::showingNow(&scheduler); }
};

}  // namespace

TEST_CASE("The status LED waits while no phone and no network reach the device") {
    Rig rig;
    CHECK(rig.showing() == StatusLedService::Showing::Waiting);
}

TEST_CASE("The status LED shows connected while a phone holds a Bluetooth connection") {
    Rig rig;
    std::snprintf(rig.ble->connection, sizeof(rig.ble->connection), "%s", "connected");
    CHECK(rig.showing() == StatusLedService::Showing::Connected);
}

TEST_CASE("The status LED shows connected while the WiFi station holds an address") {
    Rig rig;
    const uint8_t ip[4] = {192, 168, 8, 171};
    platform::setTestWifiStaIPv4(ip);
    CHECK(rig.showing() == StatusLedService::Showing::Connected);
}

TEST_CASE("The status LED shows connected while a client is on the access point, and waits on an empty one") {
    Rig rig;
    platform::setTestWifiApAvailable(true);
    REQUIRE(platform::wifiApInit({"NEBULITE-cc8200", "4.3.2.1", ""}));
    CHECK(rig.showing() == StatusLedService::Showing::Waiting);
    platform::setTestWifiApClients(1);
    CHECK(rig.showing() == StatusLedService::Showing::Connected);
    platform::setTestWifiApAvailable(false);
}

TEST_CASE("An installing update shows over a connection, whichever path installs it") {
    Rig rig;
    std::snprintf(rig.ble->connection, sizeof(rig.ble->connection), "%s", "connected");
    platform::setTestOtaBusy(true);
    CHECK(rig.showing() == StatusLedService::Showing::Updating);
    platform::setTestOtaBusy(false);
    std::snprintf(g_otaStatus, sizeof(g_otaStatus), "%s", "downloading");
    CHECK(rig.showing() == StatusLedService::Showing::Updating);
    std::snprintf(g_otaStatus, sizeof(g_otaStatus), "%s", "idle");
}

TEST_CASE("Connected is steady, waiting blinks once a second and updating five times a second") {
    using S = StatusLedService::Showing;
    for (uint32_t ms : {0u, 499u, 500u, 1700u}) CHECK(StatusLedService::lit(S::Connected, ms));
    CHECK(StatusLedService::lit(S::Waiting, 0));
    CHECK(StatusLedService::lit(S::Waiting, 499));
    CHECK_FALSE(StatusLedService::lit(S::Waiting, 500));
    CHECK(StatusLedService::lit(S::Waiting, 1000));
    CHECK(StatusLedService::lit(S::Updating, 99));
    CHECK_FALSE(StatusLedService::lit(S::Updating, 100));
    CHECK(StatusLedService::lit(S::Updating, 200));
}

TEST_CASE("The level sets the lit duty, full level fully on") {
    CHECK(StatusLedService::duty(255) == 4095);
    CHECK(StatusLedService::duty(64) == 1028);
    CHECK(StatusLedService::duty(1) > 0);
}

TEST_CASE("A set pin is driven at the level while connected, and the readout names the state") {
    Rig rig;
    rig.led->pin = 46;
    rig.led->level = 64;
    rig.led->onControlChanged("pin");
    REQUIRE(rig.led->channelForTest() >= 0);
    CHECK(platform::pwmPinForTest(rig.led->channelForTest()) == 46);
    std::snprintf(rig.ble->connection, sizeof(rig.ble->connection), "%s", "connected");
    rig.led->tick1s();
    rig.led->tick20ms();
    CHECK(platform::pwmDutyForTest(rig.led->channelForTest()) == StatusLedService::duty(64));
    CHECK(std::strcmp(rig.led->readText("showing", ""), "connected") == 0);
}

TEST_CASE("A new pin takes over live, and no pin or a disabled service drives none") {
    Rig rig;
    rig.led->pin = 46;
    rig.led->onControlChanged("pin");
    REQUIRE(rig.led->channelForTest() >= 0);
    rig.led->pin = 47;
    rig.led->onControlChanged("pin");
    REQUIRE(rig.led->channelForTest() >= 0);
    CHECK(platform::pwmPinForTest(rig.led->channelForTest()) == 47);
    rig.led->setEnabled(false);
    CHECK(rig.led->channelForTest() == -1);
    rig.led->setEnabled(true);
    CHECK(rig.led->channelForTest() >= 0);
    rig.led->pin = -1;
    rig.led->onControlChanged("pin");
    CHECK(rig.led->channelForTest() == -1);
}
