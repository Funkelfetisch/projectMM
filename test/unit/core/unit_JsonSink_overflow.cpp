/// @module JsonSink

/// Pins what happens when a heap-mode sink cannot get the memory it needs.
///
/// Both behaviors here were bugs found on the bench (2026-09-08). A classic ESP32 serving a ~40 KB state document truncated it at a power of two and shipped it as a COMPLETE WebSocket frame: the browser parsed it, threw, and every module card past the cut vanished with nothing logged anywhere. Two causes, one per test below.

#include "doctest.h"
#include "core/util/JsonSink.h"
#include "platform/platform.h"

#include <cstring>
#include <cstdint>
#include <string>

TEST_CASE("a heap sink that runs out of memory says so instead of truncating silently") {
    mm::JsonSink sink;
    sink.append("{\"a\":1");
    CHECK_FALSE(sink.overflowed());          // healthy so far

    // Grow until the allocator refuses. The append must then fail LOUDLY: a caller that ships the buffer regardless sends a document that is truncated but looks whole, which is exactly the failure this flag exists to make visible. Bounded so a 64-bit host with lots of RAM still finishes: if the allocator never refuses, the sink must simply stay healthy and complete.
    std::string chunk(64 * 1024, 'x');
    for (int i = 0; i < 4096 && !sink.overflowed(); i++) sink.append(chunk.c_str());

    if (sink.overflowed()) {
        CHECK(std::strncmp(sink.data(), "{\"a\":1", 6) == 0);   // what WAS written stays intact
        CHECK(sink.data()[sink.size()] == '\0');                // and is still a valid C string
    } else {
        CHECK(sink.size() > 64 * 1024);   // no refusal on this host: it grew, which is also correct
    }
}

TEST_CASE("a document larger than one doubling still serializes whole") {
    // Growth doubles, and old + new are both live across the copy, so a doubling asks for ~3x the current size in CONTIGUOUS memory. This document crosses several doublings: it must come out byte-for-byte complete, whatever route the allocator took to get there.
    mm::JsonSink sink;
    std::string expect;
    sink.append("[");
    expect += "[";
    for (int i = 0; i < 4000; i++) {
        char frag[32];
        std::snprintf(frag, sizeof(frag), "%s\"item%04d\"", i ? "," : "", i);
        sink.append(frag);
        expect += frag;
    }
    sink.append("]");
    expect += "]";

    CHECK_FALSE(sink.overflowed());
    REQUIRE(sink.size() == expect.size());
    CHECK(std::memcmp(sink.data(), expect.data(), expect.size()) == 0);
    CHECK(sink.data()[sink.size()] == '\0');   // still a valid C string at the far end
}

TEST_CASE("a socket JsonSink keeps write failure sticky after its connection becomes writable again") {
    struct AllocationReset { ~AllocationReset() { mm::platform::setTestAllocRefusedFrom(0); } } reset;
    bool direct = false;
    SUBCASE("staged socket") {}
    SUBCASE("socket without a staging allocation") { direct = true; }
    mm::platform::TcpConnection client;
    if (direct) mm::platform::setTestAllocRefusedFrom(1);
    mm::JsonSink sink(client);
    mm::platform::setTestAllocRefusedFrom(0);
    sink.append("first");
    sink.flush();
    REQUIRE(sink.overflowed());
    const size_t failedWritten = sink.written();
    mm::platform::TcpServer server;
    uint16_t port = 0;
    for (uint16_t candidate = 34800; candidate < 34840; ++candidate)
        if (server.open(candidate)) { port = candidate; break; }
    REQUIRE(port != 0);
    REQUIRE(client.connectStart("127.0.0.1", port));
    mm::platform::TcpConnection accepted;
    auto connected = mm::platform::TcpConnection::ConnectResult::Pending;
    for (int i = 0; i < 200 && connected == mm::platform::TcpConnection::ConnectResult::Pending; ++i) {
        if (!accepted.valid()) accepted = server.accept();
        connected = client.connectPoll();
        if (connected == mm::platform::TcpConnection::ConnectResult::Pending) mm::platform::delayMs(1);
    }
    REQUIRE(connected == mm::platform::TcpConnection::ConnectResult::Connected);
    for (int i = 0; i < 100 && !accepted.valid(); ++i) { accepted = server.accept(); mm::platform::delayMs(1); }
    REQUIRE(accepted.valid());
    sink.append("must not arrive");
    sink.appendf("nor %s", "formatted bytes");
    sink.flush();
    sink.clear();
    sink.append("nor after clear");
    sink.flush();
    CHECK(sink.overflowed());
    CHECK(sink.written() == failedWritten);
    uint8_t buffer[64];
    int received = -1;
    for (int i = 0; i < 50 && received < 0; ++i) {
        received = accepted.read(buffer, sizeof(buffer));
        if (received < 0) mm::platform::delayMs(1);
    }
    CHECK(received == -1);
}
