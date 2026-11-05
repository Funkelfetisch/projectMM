/// @module BleAdvertisement
#include "doctest.h"
#include "core/util/BleAdvertisement.h"
#include "platform/esp32/BleAdvertisementRefresh.h"
#include "helio/BleAdvertisementRegistry.h"

TEST_CASE("BLE discovery recovers after a failed start without dropping active peers") {
    using mm::platform::bleAdvertisementAction;
    using Action = mm::platform::BleAdvertisementAction;
    CHECK(bleAdvertisementAction(false, false, false, true, 5000, 0) == Action::Wait);
    CHECK(bleAdvertisementAction(true, false, true, true, 5000, 4999) == Action::Start);
    CHECK(bleAdvertisementAction(true, true, true, true, 5000, 0) == Action::Wait);
    CHECK(bleAdvertisementAction(true, true, false, true, 5999, 5000) == Action::Wait);
    CHECK(bleAdvertisementAction(true, true, false, true, 6000, 5000) == Action::Restart);
    CHECK(bleAdvertisementAction(true, true, false, true, 10, UINT32_MAX - 989) == Action::Restart);
}
#include <cstring>

TEST_CASE("BLE advertising: absent observations remain unknown independently") {
    mm::BleAdvertisementSnapshot value;
    value.powerOn = true; value.rgbw = true; value.speed = 128;
    value.red = 255; value.white = 255;
    const auto bytes = mm::encodeBleAdvertisement(value);
    CHECK(bytes.size() == 13);
    CHECK(bytes[0] == 1); CHECK(bytes[1] == 0);
    for (size_t i = 2; i < 12; ++i) CHECK(bytes[i] == 0);
    CHECK(bytes[12] == 255);
}
TEST_CASE("BLE advertising: actual values occupy the exact public v1 bytes") {
    mm::BleAdvertisementSnapshot value;
    value.product = {1, 48}; value.ledCount = 222;
    value.powerKnown = true; value.powerOn = true; value.rgbwKnown = true; value.rgbw = true;
    value.animation = mm::ble_advertisement::sceneCode("colortwinkles");
    value.speedKnown = true; value.speed = 248;
    value.rgbKnown = true; value.red = 255; value.green = 128; value.blue = 0;
    value.whiteKnown = true; value.white = 127; value.palette = 28;
    const std::array<uint8_t, 13> expected{1,127,1,222,0,48,3,248,0,0,252,127,28};
    CHECK(mm::encodeBleAdvertisement(value) == expected);
}
TEST_CASE("BLE advertising: raw signed speed preserves integer units and rejects overflow without clamping") {
    mm::BleAdvertisementSnapshot value; value.speedKnown = true;
    value.speed = -32768; auto bytes = mm::encodeBleAdvertisement(value);
    CHECK(bytes[1] == 16); CHECK(bytes[7] == 0); CHECK(bytes[8] == 128);
    value.speed = 32767; bytes = mm::encodeBleAdvertisement(value);
    CHECK(bytes[1] == 16); CHECK(bytes[7] == 255); CHECK(bytes[8] == 127);
    for (const int32_t speed : {-32769,32768}) {
        value.speed = speed; bytes = mm::encodeBleAdvertisement(value);
        CHECK(bytes[1] == 0); CHECK(bytes[7] == 0); CHECK(bytes[8] == 0);
    }
}
TEST_CASE("BLE advertising: unrepresentable count and colour are unknown rather than truncated") {
    mm::BleAdvertisementSnapshot value;
    value.ledCount = 65536; value.product = {7,0};
    value.rgbKnown = true; value.red = 256; value.green = 0; value.blue = 0;
    value.whiteKnown = true; value.white = -1;
    const auto bytes = mm::encodeBleAdvertisement(value);
    CHECK(bytes[1] == 0); CHECK(bytes[2] == 0); CHECK(bytes[3] == 0); CHECK(bytes[4] == 0);
    CHECK(bytes[5] == 0); CHECK(bytes[9] == 0); CHECK(bytes[10] == 0); CHECK(bytes[11] == 0);
    value.ledCount = 65535;
    const auto maximum = mm::encodeBleAdvertisement(value);
    CHECK(maximum[3] == 255); CHECK(maximum[4] == 255);
}
TEST_CASE("BLE advertising: zero values remain known and physical size is never inferred") {
    mm::BleAdvertisementSnapshot value;
    value.product = {1,0}; value.ledCount = 222;
    value.powerKnown = true; value.rgbwKnown = true;
    value.speedKnown = true; value.rgbKnown = true; value.whiteKnown = true;
    const auto bytes = mm::encodeBleAdvertisement(value);
    CHECK(bytes[1] == 53); CHECK(bytes[5] == 0);
    CHECK(bytes[7] == 0); CHECK(bytes[8] == 0); CHECK(bytes[9] == 0); CHECK(bytes[10] == 0); CHECK(bytes[11] == 0);
}
TEST_CASE("BLE advertising: named registries are explicit unique stable wire identities") {
    CHECK(mm::ble_advertisement::sceneCode("solid") == 1);
    CHECK(mm::ble_advertisement::sceneCode("twin-suns") == 24);
    CHECK(mm::ble_advertisement::sceneCode("custom") == 0);
    CHECK(mm::ble_advertisement::paletteCode("Rainbow") == 0);
    CHECK(mm::ble_advertisement::paletteCode("Hult") == 28);
    CHECK(mm::ble_advertisement::paletteCode("my-script") == 255);
    for (const auto& a : mm::ble_advertisement::scenes)
        for (const auto& b : mm::ble_advertisement::scenes)
            if (&a != &b) { CHECK(a.code != b.code); CHECK(std::strcmp(a.name,b.name) != 0); }
    for (const auto& a : mm::ble_advertisement::palettes)
        for (const auto& b : mm::ble_advertisement::palettes)
            if (&a != &b) { CHECK(a.code != b.code); CHECK(std::strcmp(a.name,b.name) != 0); }
}

TEST_CASE("BLE advertising: native white requires known RGBW and non-spheres cannot claim diameter") {
    mm::BleAdvertisementSnapshot value;
    value.whiteKnown = true; value.white = 111;
    value.rgbw = true; // an unconfirmed RGBW bit never turns into a capability.
    value.product = {16,50};
    auto bytes = mm::encodeBleAdvertisement(value);
    CHECK((bytes[1] & 12) == 0); CHECK((bytes[1] & 64) == 0); CHECK(bytes[11] == 0); CHECK(bytes[5] == 0);
    value.rgbwKnown = true; value.rgbw = false;
    bytes = mm::encodeBleAdvertisement(value);
    CHECK((bytes[1] & 12) == 4); CHECK((bytes[1] & 64) == 0); CHECK(bytes[11] == 0);
    value.rgbw = true;
    bytes = mm::encodeBleAdvertisement(value);
    CHECK((bytes[1] & 12) == 12); CHECK((bytes[1] & 64) == 64); CHECK(bytes[11] == 111);
    value.product = {2,8}; bytes = mm::encodeBleAdvertisement(value); CHECK(bytes[5] == 8);
    // The 8 cm necklace is worn, not a sphere: its code goes out, a diameter never does.
    value.product = {3,8}; bytes = mm::encodeBleAdvertisement(value); CHECK(bytes[2] == 3); CHECK(bytes[5] == 0);
    value.product = {4,0}; bytes = mm::encodeBleAdvertisement(value); CHECK(bytes[2] == 0);
    value.product = {0,50}; bytes = mm::encodeBleAdvertisement(value); CHECK(bytes[5] == 0);
}

TEST_CASE("BLE advertising: only exact explicitly selected NEBULITE models identify products") {
    using mm::ble_advertisement::productCodeForModel;
    CHECK(productCodeForModel("NEBULITE Croptop S3") == 20);
    CHECK(productCodeForModel("NEBULITE Croptop ESP32") == 20);
    CHECK(productCodeForModel("NEBULITE Jacket S3") == 22);
    CHECK(productCodeForModel("NEBULITE Jacket ESP32") == 22);
    CHECK(productCodeForModel("NEBULITE Fanny pack ESP32") == 16);
    CHECK(productCodeForModel("NEBULITE Backpack ESP32") == 18);
    CHECK(productCodeForModel("HELIO necklace") == 3);
    CHECK(productCodeForModel("HELIO Necklace") == 0);
    CHECK(productCodeForModel("NEBULITE Vest S3") == 0);
    CHECK(productCodeForModel("NEBULITE Croptop") == 0);
    CHECK(productCodeForModel(nullptr) == 0);
}
