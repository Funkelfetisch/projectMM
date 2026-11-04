/// @module BleConnectionPolicy
#include "doctest.h"
#include "platform/esp32/BleConnectionPolicy.h"

TEST_CASE("BLE connection: a slow interval or any latency asks for 15 to 30 ms with no latency and four seconds supervision") {
    mm::platform::BleConnectionHint hint{1,2,3,4};
    REQUIRE(mm::platform::bleConnectionHint(39, 0, 42, hint));   // 48.75 ms, BlueZ's default range
    CHECK(hint.minInterval == 12); CHECK(hint.maxInterval == 24); CHECK(hint.latency == 0); CHECK(hint.timeout == 400);
    REQUIRE(mm::platform::bleConnectionHint(24, 3, 100, hint));   // fast enough, but latency skips events
    CHECK(hint.minInterval == 12); CHECK(hint.maxInterval == 24); CHECK(hint.latency == 0); CHECK(hint.timeout == 400);
    REQUIRE(mm::platform::bleConnectionHint(40, 0, 400, hint));   // a sufficient timeout still gets the faster interval
    CHECK(hint.minInterval == 12); CHECK(hint.maxInterval == 24); CHECK(hint.timeout == 400);
    REQUIRE(mm::platform::bleConnectionHint(39, 0, 1000, hint));   // a longer timeout than four seconds stays
    CHECK(hint.minInterval == 12); CHECK(hint.maxInterval == 24); CHECK(hint.timeout == 1000);
}

TEST_CASE("BLE connection: a faster central keeps its interval, and only a short timeout is lengthened") {
    mm::platform::BleConnectionHint hint{1,2,3,4};
    REQUIRE(mm::platform::bleConnectionHint(9, 0, 42, hint));   // 11.25 ms, Android's high priority
    CHECK(hint.minInterval == 9); CHECK(hint.maxInterval == 9); CHECK(hint.latency == 0); CHECK(hint.timeout == 400);
    CHECK_FALSE(mm::platform::bleConnectionHint(9, 0, 500, hint));
    CHECK_FALSE(mm::platform::bleConnectionHint(24, 0, 400, hint));   // exactly 30 ms is fast enough
}

TEST_CASE("BLE connection: invalid parameters never request an update") {
    using mm::platform::bleConnectionHint;
    mm::platform::BleConnectionHint hint{1,2,3,4};
    CHECK_FALSE(bleConnectionHint(5, 0, 42, hint));
    CHECK_FALSE(bleConnectionHint(3201, 0, 400, hint));
    CHECK_FALSE(bleConnectionHint(39, 500, 42, hint));
    CHECK_FALSE(bleConnectionHint(39, 0, 9, hint));
    CHECK_FALSE(bleConnectionHint(39, 0, 3201, hint));
    CHECK_FALSE(bleConnectionHint(40, 3, 40, hint)); // equality violates the strict timeout bound
    CHECK(hint.minInterval == 1); CHECK(hint.maxInterval == 2); CHECK(hint.latency == 3); CHECK(hint.timeout == 4);
}
