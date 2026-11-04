/// @module platform
#include "doctest.h"
#include "core/util/BlePeerIo.h"
#include <cstring>
#include <string>

namespace {
/// Drain a peer's reply in chunks of `size` bytes, as notifications at one ATT MTU carry it.
std::string drain(mm::BlePeerIo& io, size_t size, size_t& chunks) {
    std::string out; uint8_t chunk[520] = {};
    chunks = 0;
    while (size_t count = io.pending(chunk, size)) { out.append(reinterpret_cast<char*>(chunk), count); io.sent(count); ++chunks; }
    return out;
}
}

TEST_CASE("BLE peer IO: requests frame per peer, and a reply goes out with its LF in chunks of the link's size") {
    mm::BlePeerIo a, b;
    const uint8_t first[] = {'a'}, second[] = {'b', '\n'};
    CHECK(a.feed(first, sizeof(first)) == mm::BleMessage::Result::Partial);
    CHECK(b.feed(second, sizeof(second)) == mm::BleMessage::Result::Ready);
    char out[8] = {};
    CHECK(b.take(out, sizeof(out)) == 1); CHECK(std::strcmp(out, "b") == 0);
    CHECK(a.feed(second, sizeof(second)) == mm::BleMessage::Result::Ready);
    CHECK(a.take(out, sizeof(out)) == 2); CHECK(std::strcmp(out, "ab") == 0);
    REQUIRE(a.publish("first", true)); REQUIRE(b.publish("other", true));
    size_t chunks = 0;
    CHECK(drain(a, 3, chunks) == "first\n"); CHECK(chunks == 2);
    CHECK(drain(b, 20, chunks) == "other\n"); CHECK(chunks == 1);
    CHECK(std::strcmp(a.current(), "first") == 0);
    CHECK_FALSE(a.sending());
}

TEST_CASE("BLE peer IO: a reply still going out holds the next, and one for a peer without notifications never does") {
    mm::BlePeerIo io;
    REQUIRE(io.publish("first", true));
    CHECK(io.sending());
    CHECK_FALSE(io.publish("later", true));
    size_t chunks = 0;
    CHECK(drain(io, 2, chunks) == "first\n");
    REQUIRE(io.publish("later", true));
    CHECK(drain(io, 64, chunks) == "later\n");
    REQUIRE(io.publish("quiet", false));
    CHECK_FALSE(io.sending());
    uint8_t chunk[8] = {};
    CHECK(io.pending(chunk, sizeof(chunk)) == 0);
    CHECK(std::strcmp(io.current(), "quiet") == 0);
    CHECK(io.publish("next", true));
}

TEST_CASE("BLE peer IO: a 512-byte reply crosses the minimum MTU whole, and a longer one is refused") {
    mm::BlePeerIo io;
    const std::string full(512, 'r');
    REQUIRE(io.publish(full.c_str(), true));
    size_t chunks = 0;
    CHECK(drain(io, 20, chunks) == full + "\n");
    CHECK(chunks == 26);
    const std::string over(513, 'r');
    CHECK_FALSE(io.publish(over.c_str(), true));
    CHECK(std::strcmp(io.current(), full.c_str()) == 0);
}

TEST_CASE("BLE peer IO: disconnect drops partial input and the unsent reply, for that peer only") {
    mm::BlePeerIo a, b;
    const uint8_t partial[] = {'x'}, line[] = {'y', '\n'};
    CHECK(a.feed(partial, sizeof(partial)) == mm::BleMessage::Result::Partial);
    CHECK(b.feed(partial, sizeof(partial)) == mm::BleMessage::Result::Partial);
    REQUIRE(a.publish("old", true)); REQUIRE(b.publish("kept", true));
    a.disconnect();
    CHECK_FALSE(a.sending());
    CHECK(b.sending());
    CHECK(a.feed(line, sizeof(line)) == mm::BleMessage::Result::Ready);
    CHECK(b.feed(line, sizeof(line)) == mm::BleMessage::Result::Ready);
    char out[8] = {};
    CHECK(a.take(out, sizeof(out)) == 1); CHECK(std::strcmp(out, "y") == 0);
    CHECK(b.take(out, sizeof(out)) == 2); CHECK(std::strcmp(out, "xy") == 0);
}
