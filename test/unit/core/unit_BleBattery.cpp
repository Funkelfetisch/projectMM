/// @module platform

#include "doctest.h"
#include "platform/BleBattery.h"

#include <array>
#include <cstdint>

TEST_CASE("BLE battery: service identities are finite, unique and explicitly declared") {
    using Config = mm::platform::BleBatteryConfig;
    CHECK(Config{}.valid());
    CHECK((Config{1, {1, 0}}.valid()));
    CHECK((Config{1, {2, 0}}.valid()));
    CHECK((Config{2, {1, 2}}.valid()));
    CHECK_FALSE((Config{3, {1, 2}}.valid()));
    CHECK_FALSE((Config{2, {1, 1}}.valid()));
    CHECK_FALSE((Config{1, {0, 0}}.valid()));
    CHECK_FALSE((Config{1, {3, 0}}.valid()));
    CHECK_FALSE((Config{0, {1, 0}}.valid()));
}

TEST_CASE("BLE battery: CPF identifies first and second percentage values in SIG byte order") {
    const std::array<uint8_t, 7> first{0x04, 0, 0xAD, 0x27, 1, 1, 0};
    const std::array<uint8_t, 7> second{0x04, 0, 0xAD, 0x27, 1, 2, 0};
    CHECK(mm::platform::BleBatteryConfig::presentation(1) == first);
    CHECK(mm::platform::BleBatteryConfig::presentation(2) == second);
    CHECK((mm::platform::BleBatteryConfig::presentation(0) == std::array<uint8_t, 7>{}));
}

TEST_CASE("BLE battery: unknown and stale measurements cannot become a level byte") {
    mm::platform::BleBatteryValue value; uint8_t level = 77;
    CHECK_FALSE(value.readLevel(level)); CHECK(level == 77);
    value.present = true; value.levelValid = true; value.level = 50;
    CHECK_FALSE(value.readLevel(level)); CHECK(level == 77);
    value.presenceKnown = true; value.levelValid = false;
    CHECK_FALSE(value.readLevel(level)); CHECK(level == 77);
}

TEST_CASE("BLE battery: real empty and absent values follow BAS without using an RFU sentinel") {
    mm::platform::BleBatteryValue value{true, true, true, 0}; uint8_t level = 77;
    REQUIRE(value.readLevel(level)); CHECK(level == 0);
    value.level = 100; REQUIRE(value.readLevel(level)); CHECK(level == 100);
    value.level = 255; CHECK_FALSE(value.readLevel(level)); CHECK(level == 100);
    value.present = false; value.levelValid = false;
    REQUIRE(value.readLevel(level)); CHECK(level == 0);
}

TEST_CASE("BLE battery: a failed notification retries the latest snapshot on the next publish") {
    mm::platform::BleBatteryUpdates updates;
    mm::platform::BleBatterySnapshot first;
    first.slots[0] = {true, true, true, 42};
    CHECK(updates.publish(first, 0));
    CHECK_FALSE(updates.publish(first, 1));
    updates.deliveryFailed();
    CHECK(updates.publish(first, 2)); // unchanged level still needs another delivery attempt
    CHECK_FALSE(updates.publish(first, 3));
    updates.deliveryFailed();
    auto next = first;
    next.slots[0].level = 43;
    next.slots[1] = {true, true, true, 75};
    CHECK(updates.publish(next, 4));
    CHECK(updates.values == next);
    CHECK_FALSE(updates.publish(next, 5));
    updates.deliveryFailed();
    next.slots[0].levelValid = false;
    CHECK(updates.publish(next, 6));
    uint8_t level = 99;
    CHECK_FALSE(updates.values.slots[0].readLevel(level)); CHECK(level == 99);
    REQUIRE(updates.values.slots[1].readLevel(level)); CHECK(level == 75);
}

TEST_CASE("BLE battery: publisher timeout and recovery across uptime wrap") {
    mm::platform::BleBatteryUpdates updates;
    mm::platform::BleBatterySnapshot value;
    value.slots[0] = {true, true, true, 55};
    CHECK_FALSE(updates.fresh(0));
    CHECK(updates.publish(value, 0xfffffff0u));
    CHECK(updates.fresh(0x00000010u));
    CHECK_FALSE(updates.fresh(0xfffffff0u + updates.kPublishTimeoutMs));
    // A resumed identical level must notify again after a publication gap.
    CHECK(updates.publish(value, 0xfffffff0u + updates.kPublishTimeoutMs));
    CHECK(updates.fresh(0xfffffff0u + updates.kPublishTimeoutMs));
    CHECK_FALSE(updates.publish(value, 0xfffffff1u + updates.kPublishTimeoutMs));
}
