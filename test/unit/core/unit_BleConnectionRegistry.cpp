/// @module platform
#include "doctest.h"
#include "core/util/BleConnectionRegistry.h"

TEST_CASE("BLE connection identities: independent peers and full capacity never evict") {
    mm::BleConnectionRegistry<2> peers;
    const auto a = peers.connect(7), b = peers.connect(8);
    REQUIRE(a); REQUIRE(b); CHECK(a != b); CHECK(peers.size() == 2);
    CHECK_FALSE(peers.connect(9)); CHECK_FALSE(peers.connect(7));
    CHECK(peers.contains(a)); CHECK(peers.contains(b));
    CHECK(peers.disconnect(a)); CHECK_FALSE(peers.contains(a)); CHECK(peers.contains(b));
    const auto c = peers.connect(9); REQUIRE(c); CHECK(peers.size() == 2);
}

TEST_CASE("BLE connection identities: handle reuse rejects delayed replies and stale disconnects") {
    mm::BleConnectionRegistry<1> peers;
    const auto old = peers.connect(42); REQUIRE(old);
    REQUIRE(peers.disconnect(old));
    const auto current = peers.connect(42); REQUIRE(current);
    CHECK(current.handle == old.handle); CHECK(current.generation != old.generation);
    CHECK_FALSE(peers.contains(old)); CHECK_FALSE(peers.disconnect(old));
    CHECK(peers.contains(current)); CHECK(peers.size() == 1);
    CHECK_FALSE(peers.connect(UINT16_MAX));
}
