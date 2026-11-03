/// @module platform
#include "doctest.h"
#include "core/util/BlePeerIo.h"
#include <cstring>

TEST_CASE("BLE peer IO: fragments and stable long-read snapshots remain independent") {
    mm::BlePeerIo a, b;
    const uint8_t first[] = {'a'}, second[] = {'b', '\n'};
    CHECK(a.feed(first, sizeof(first)) == mm::BleMessage::Result::Partial);
    CHECK(b.feed(second, sizeof(second)) == mm::BleMessage::Result::Ready);
    char out[8] = {};
    CHECK(b.take(out, sizeof(out)) == 1); CHECK(std::strcmp(out, "b") == 0);
    CHECK(a.feed(second, sizeof(second)) == mm::BleMessage::Result::Ready);
    CHECK(a.take(out, sizeof(out)) == 2); CHECK(std::strcmp(out, "ab") == 0);
    REQUIRE(a.publish("first")); REQUIRE(b.publish("other"));
    CHECK(std::strcmp(a.read(0), "first") == 0);
    REQUIRE(a.publish("later"));
    CHECK(std::strcmp(a.read(1), "first") == 0);
    CHECK(std::strcmp(b.read(0), "other") == 0);
    CHECK(std::strcmp(a.read(0), "later") == 0);
}

TEST_CASE("BLE peer IO: disconnect discards partial input and read snapshot only for that peer") {
    mm::BlePeerIo a, b;
    const uint8_t partial[] = {'x'}, line[] = {'y', '\n'};
    CHECK(a.feed(partial, sizeof(partial)) == mm::BleMessage::Result::Partial);
    CHECK(b.feed(partial, sizeof(partial)) == mm::BleMessage::Result::Partial);
    REQUIRE(a.publish("old")); CHECK(std::strcmp(a.read(0), "old") == 0);
    REQUIRE(a.publish("new")); a.disconnect();
    CHECK(std::strcmp(a.read(1), "new") == 0);
    CHECK(a.feed(line, sizeof(line)) == mm::BleMessage::Result::Ready);
    CHECK(b.feed(line, sizeof(line)) == mm::BleMessage::Result::Ready);
    char out[8] = {};
    CHECK(a.take(out, sizeof(out)) == 1); CHECK(std::strcmp(out, "y") == 0);
    CHECK(b.take(out, sizeof(out)) == 2); CHECK(std::strcmp(out, "xy") == 0);
}
