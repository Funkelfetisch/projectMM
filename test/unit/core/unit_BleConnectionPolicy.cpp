/// @module BleConnectionPolicy
#include "doctest.h"
#include "platform/esp32/BleConnectionPolicy.h"

TEST_CASE("BLE connection: short valid supervision keeps the central's interval and latency") {
    mm::platform::BleConnectionHint hint{1,2,3};
    REQUIRE(mm::platform::bleConnectionHint(39, 0, 42, hint));
    CHECK(hint.interval == 39); CHECK(hint.latency == 0); CHECK(hint.timeout == 400);
    REQUIRE(mm::platform::bleConnectionHint(24, 3, 100, hint));
    CHECK(hint.interval == 24); CHECK(hint.latency == 3); CHECK(hint.timeout == 400);
}

TEST_CASE("BLE connection: sufficient or invalid parameters never request an update") {
    using mm::platform::bleConnectionHint;
    mm::platform::BleConnectionHint hint{1,2,3};
    CHECK_FALSE(bleConnectionHint(39, 0, 400, hint));
    CHECK_FALSE(bleConnectionHint(39, 0, 1000, hint));
    CHECK_FALSE(bleConnectionHint(5, 0, 42, hint));
    CHECK_FALSE(bleConnectionHint(3201, 0, 400, hint));
    CHECK_FALSE(bleConnectionHint(39, 500, 42, hint));
    CHECK_FALSE(bleConnectionHint(39, 0, 9, hint));
    CHECK_FALSE(bleConnectionHint(39, 0, 3201, hint));
    CHECK_FALSE(bleConnectionHint(40, 3, 40, hint)); // equality violates the strict timeout bound
    CHECK(hint.interval == 1); CHECK(hint.latency == 2); CHECK(hint.timeout == 3);
}
