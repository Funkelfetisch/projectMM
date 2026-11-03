/// @module platform
#include "doctest.h"
#include "core/util/BlePeerTable.h"
#include "platform/esp32/BlePeerDisconnect.h"
#include <cstring>

namespace {
struct PeerAllocatorProbe {
    static inline unsigned allocated = 0, freed = 0;
    static inline bool fail = false;
    static mm::BlePeerIo* create() { if (fail) return nullptr; ++allocated; return new mm::BlePeerIo(); }
    static void destroy(mm::BlePeerIo* io) { if (io) { ++freed; delete io; } }
};
}

TEST_CASE("BLE peer table: interleaved requests and long reads stay with their peers") {
    mm::BlePeerTable<2> peers;
    using Result = mm::BleMessage::Result;
    using Publish = mm::BlePeerTable<2>::Publish;
    REQUIRE(peers.publish("initial", 0) == Publish::Stored);
    REQUIRE(peers.connect(1)); REQUIRE(peers.connect(2));
    char empty[8] = {}; uint64_t unchanged = 123;
    CHECK(peers.take(empty, sizeof(empty), unchanged) == 0);
    CHECK(unchanged == 123);
    CHECK(std::strcmp(peers.read(1, 0), "initial") == 0);
    CHECK_FALSE(peers.connect(3)); CHECK_FALSE(peers.connect(1));
    const uint8_t a[] = {'a'}, b[] = {'b', '\n'}, end[] = {'\n'};
    CHECK(peers.feed(1, a, sizeof(a)) == Result::Partial);
    CHECK(peers.feed(2, b, sizeof(b)) == Result::Ready);
    CHECK(peers.feed(1, end, sizeof(end)) == Result::Ready);
    char out[8] = {}; uint64_t first = 0, second = 0;
    REQUIRE(peers.take(out, sizeof(out), first) == 1); CHECK(std::strcmp(out, "a") == 0);
    REQUIRE(peers.take(out, sizeof(out), second) == 1); CHECK(std::strcmp(out, "b") == 0);
    CHECK(first != second);
    REQUIRE(peers.publish("alpha", first) == Publish::Stored);
    REQUIRE(peers.publish("bravo", second) == Publish::Stored);
    CHECK(std::strcmp(peers.read(1, 0), "alpha") == 0);
    REQUIRE(peers.publish("updated", first) == Publish::Stored);
    CHECK(std::strcmp(peers.read(1, 1), "alpha") == 0);
    CHECK(std::strcmp(peers.read(2, 0), "bravo") == 0);
    CHECK(std::strcmp(peers.read(1, 0), "updated") == 0);
    CHECK(peers.feed(1, a, sizeof(a)) == Result::Partial);
    REQUIRE(peers.disconnect(1)); REQUIRE(peers.connect(1));
    CHECK(peers.publish("delayed", first) == Publish::Stale);
    CHECK(std::strcmp(peers.read(2, 0), "bravo") == 0);
    CHECK(std::strcmp(peers.read(1, 0), "delayed") != 0);
    CHECK(std::strcmp(peers.read(1, 1), "initial") == 0);
    CHECK(peers.feed(1, b, sizeof(b)) == Result::Ready);
    REQUIRE(peers.take(out, sizeof(out), first) == 1); CHECK(std::strcmp(out, "b") == 0);
}

TEST_CASE("BLE peer table: subscriptions belong to the connection and physical battery slot") {
    mm::BlePeerTable<2> peers;
    REQUIRE(peers.connect(10)); REQUIRE(peers.connect(20));
    REQUIRE(peers.subscribe(10, 0, true)); REQUIRE(peers.subscribe(10, 1, true));
    REQUIRE(peers.subscribe(20, 2, true));
    uint16_t targets[2] = {};
    REQUIRE(peers.subscribers(0, targets, 2) == 1); CHECK(targets[0] == 10);
    REQUIRE(peers.subscribers(1, targets, 2) == 1); CHECK(targets[0] == 10);
    REQUIRE(peers.subscribers(2, targets, 2) == 1); CHECK(targets[0] == 20);
    CHECK_FALSE(peers.subscribe(99, 0, true)); CHECK_FALSE(peers.subscribe(10, 8, true));
    REQUIRE(peers.disconnect(10)); REQUIRE(peers.connect(10));
    CHECK(peers.subscribers(0, targets, 2) == 0);
    CHECK(peers.subscribers(1, targets, 2) == 0);
    REQUIRE(peers.subscribers(2, targets, 2) == 1); CHECK(targets[0] == 20);
    REQUIRE(peers.subscribe(20, 2, false)); CHECK(peers.subscribers(2, targets, 2) == 0);
}

TEST_CASE("BLE peer table: malformed partial input cannot interrupt another client") {
    mm::BlePeerTable<2> peers;
    using Result = mm::BleMessage::Result;
    REQUIRE(peers.connect(1)); REQUIRE(peers.connect(2));
    const uint8_t bad[] = {'{', 0};
    CHECK(peers.feed(1, bad, sizeof(bad)) == Result::Invalid);
    const uint8_t good[] = {'{', '}', '\n'};
    REQUIRE(peers.feed(2, good, sizeof(good)) == Result::Ready);
    char out[8] = {}; uint64_t generation = 0;
    REQUIRE(peers.take(out, sizeof(out), generation) == 2);
    CHECK(std::strcmp(out, "{}") == 0);
    CHECK(peers.publish("ok", generation) == mm::BlePeerTable<2>::Publish::Stored);
    CHECK(std::strcmp(peers.read(2, 0), "ok") == 0);
    // A rejected incomplete line is discarded through its delimiter.
    const uint8_t end[] = {'\n'};
    CHECK(peers.feed(1, end, sizeof(end)) == Result::Invalid);
    REQUIRE(peers.feed(1, good, sizeof(good)) == Result::Ready);
    REQUIRE(peers.take(out, sizeof(out), generation) == 2);
    CHECK(std::strcmp(out, "{}") == 0);
}

TEST_CASE("BLE peer table: a busy peer does not starve other connected clients") {
    mm::BlePeerTable<8> peers;
    using Result = mm::BleMessage::Result;
    for (uint16_t handle = 1; handle <= 8; ++handle) {
        REQUIRE(peers.connect(handle));
        const uint8_t request[] = {static_cast<uint8_t>('0' + handle), '\n'};
        REQUIRE(peers.feed(handle, request, sizeof(request)) == Result::Ready);
    }
    char value[8] = {}; uint64_t owner = 0;
    for (uint16_t expected = 1; expected <= 8; ++expected) {
        REQUIRE(peers.take(value, sizeof(value), owner) == 1);
        CHECK(value[0] == static_cast<char>('0' + expected));
        // The first client immediately submits another command after its reply.
        if (expected == 1) {
            const uint8_t next[] = {'a', '\n'};
            REQUIRE(peers.feed(1, next, sizeof(next)) == Result::Ready);
        }
    }
    REQUIRE(peers.take(value, sizeof(value), owner) == 1);
    CHECK(value[0] == 'a');
}

TEST_CASE("BLE peer table: eight clients keep requests and responses separate across reconnect") {
    mm::BlePeerTable<8> peers;
    using Publish = mm::BlePeerTable<8>::Publish;
    uint64_t generations[8] = {};
    for (uint16_t handle = 1; handle <= 8; ++handle) {
        REQUIRE(peers.connect(handle));
        const uint8_t request[] = {static_cast<uint8_t>('0' + handle), '\n'};
        REQUIRE(peers.feed(handle, request, sizeof(request)) == mm::BleMessage::Result::Ready);
        REQUIRE(peers.subscribe(handle, 1, true));
    }
    CHECK_FALSE(peers.connect(9));
    uint16_t subscribers[8] = {};
    CHECK(peers.subscribers(1, subscribers, 8) == 8);
    for (unsigned i = 0; i < 8; ++i) {
        char request[8] = {};
        REQUIRE(peers.take(request, sizeof(request), generations[i]) == 1);
        CHECK(request[0] == static_cast<char>('1' + i));
        const char reply[] = {static_cast<char>('a' + i), '\0'};
        REQUIRE(peers.publish(reply, generations[i]) == Publish::Stored);
    }
    for (unsigned i = 0; i < 8; ++i)
        CHECK(peers.read(static_cast<uint16_t>(i + 1), 0)[0] == static_cast<char>('a' + i));
    REQUIRE(peers.disconnect(4));
    REQUIRE(peers.connect(9));
    CHECK(peers.publish("late", generations[3]) == Publish::Stale);
    CHECK(peers.subscribers(1, subscribers, 8) == 7);
    for (unsigned i = 0; i < 8; ++i) if (i != 3)
        CHECK(peers.read(static_cast<uint16_t>(i + 1), 0)[0] == static_cast<char>('a' + i));
}

TEST_CASE("BLE peer table: extra peers allocate on demand and a failed allocation preserves existing clients") {
    PeerAllocatorProbe::allocated = PeerAllocatorProbe::freed = 0; PeerAllocatorProbe::fail = false;
    {
        mm::BlePeerTable<8, PeerAllocatorProbe> peers;
        REQUIRE(peers.connect(1)); REQUIRE(peers.connect(2));
        CHECK(PeerAllocatorProbe::allocated == 0);
        PeerAllocatorProbe::fail = true;
        CHECK_FALSE(peers.connect(3)); CHECK(peers.size() == 2);
        CHECK(peers.read(1, 0) != nullptr); CHECK(peers.read(2, 0) != nullptr);
        PeerAllocatorProbe::fail = false;
        REQUIRE(peers.connect(3)); CHECK(PeerAllocatorProbe::allocated == 1);
        REQUIRE(peers.disconnect(3)); CHECK(PeerAllocatorProbe::freed == 1);
        REQUIRE(peers.connect(4)); CHECK(PeerAllocatorProbe::allocated == 2);
    }
    CHECK(PeerAllocatorProbe::freed == 2);
}

TEST_CASE("BLE peer table: repeated extra-peer reconnects free each buffer and cannot inherit replies") {
    PeerAllocatorProbe::allocated = PeerAllocatorProbe::freed = 0; PeerAllocatorProbe::fail = false;
    {
        mm::BlePeerTable<8, PeerAllocatorProbe> peers;
        REQUIRE(peers.connect(1)); REQUIRE(peers.connect(2));
        for (unsigned i = 0; i < 1000; ++i) {
            REQUIRE(peers.connect(3));
            REQUIRE(peers.subscribe(3, 1, true));
            const uint8_t msg[] = {'x', '\n'};
            REQUIRE(peers.feed(3, msg, sizeof(msg)) == mm::BleMessage::Result::Ready);
            char value[8] = {}; uint64_t generation = 0;
            REQUIRE(peers.take(value, sizeof(value), generation) == 1);
            REQUIRE(peers.publish("secret", generation) == decltype(peers)::Publish::Stored);
            REQUIRE(peers.disconnect(3));
            CHECK(peers.publish("late", generation) == decltype(peers)::Publish::Stale);
            uint16_t subscribers[8] = {};
            CHECK(peers.subscribers(1, subscribers, 8) == 0);
        }
        REQUIRE(peers.connect(3));
        CHECK(std::strcmp(peers.read(3, 0), "secret") != 0);
    }
    CHECK(PeerAllocatorProbe::allocated == 1001);
    CHECK(PeerAllocatorProbe::freed == PeerAllocatorProbe::allocated);
}

TEST_CASE("BLE peer disconnect: rejected and registered peers both restart advertising") {
    struct Pairing { unsigned cancelled = 0; void cancel() { ++cancelled; } };
    struct Peripheral {
        std::mutex mutex; mm::BlePeerTable<2> peers; Pairing repeatPairing;
        std::atomic<uint16_t> connection{1}; std::atomic<uint8_t> connectionCount{2};
        std::atomic<int32_t> lastDisconnectReason{-1}; std::atomic<uint32_t> linkDisconnected{0};
    } peripheral;
    REQUIRE(peripheral.peers.connect(1)); REQUIRE(peripheral.peers.connect(2));
    unsigned advertised = 0;
    auto advertise = [&] { const bool locked = peripheral.mutex.try_lock(); CHECK(locked); if (locked) peripheral.mutex.unlock(); ++advertised; };
    mm::platform::blePeerDisconnected(peripheral, 3, 19, advertise);
    CHECK(advertised == 1); CHECK(peripheral.peers.size() == 2);
    CHECK(peripheral.connection.load() == 1); CHECK(peripheral.connectionCount.load() == 2);
    CHECK(peripheral.repeatPairing.cancelled == 0); CHECK(peripheral.lastDisconnectReason.load() == 19);
    mm::platform::blePeerDisconnected(peripheral, 1, 22, advertise);
    CHECK(advertised == 2); CHECK(peripheral.peers.size() == 1);
    CHECK(peripheral.connection.load() == 2); CHECK(peripheral.connectionCount.load() == 1);
    CHECK(peripheral.repeatPairing.cancelled == 1);
    mm::platform::blePeerDisconnected(peripheral, 2, 8, advertise);
    CHECK(advertised == 3); CHECK(peripheral.peers.size() == 0);
    CHECK(peripheral.connection.load() == UINT16_MAX); CHECK(peripheral.connectionCount.load() == 0);
    CHECK(peripheral.linkDisconnected.load() == 3);
}

TEST_CASE("BLE peer table: link security belongs to one connection and starts open on every reconnect") {
    using Link = mm::platform::BleLinkSecurity;
    mm::BlePeerTable<2> peers;
    REQUIRE(peers.connect(10)); REQUIRE(peers.connect(20));
    const uint8_t frame[] = {'x', '\n'};
    char out[4] = {}; uint64_t first = 0, second = 0;
    REQUIRE(peers.feed(10, frame, sizeof(frame)) == mm::BleMessage::Result::Ready);
    REQUIRE(peers.feed(20, frame, sizeof(frame)) == mm::BleMessage::Result::Ready);
    REQUIRE(peers.take(out, sizeof(out), first) == 1); REQUIRE(peers.take(out, sizeof(out), second) == 1);
    uint16_t handle = 0;
    REQUIRE(peers.security(first, handle) != nullptr);
    CHECK(handle == 10); CHECK(*peers.security(first, handle) == Link::Open);
    CHECK(peers.secured(10, true));
    CHECK(*peers.security(first, handle) == Link::Encrypted);
    CHECK(*peers.security(second, handle) == Link::Open);
    CHECK(peers.secured(10, false));
    CHECK(*peers.security(first, handle) == Link::Encrypted);
    CHECK(peers.secured(20, false));
    CHECK(*peers.security(second, handle) == Link::Failed);
    CHECK_FALSE(peers.secured(30, true));
    REQUIRE(peers.disconnect(10));
    CHECK(peers.security(first, handle) == nullptr);
    REQUIRE(peers.connect(10));
    REQUIRE(peers.feed(10, frame, sizeof(frame)) == mm::BleMessage::Result::Ready);
    uint64_t again = 0; REQUIRE(peers.take(out, sizeof(out), again) == 1);
    CHECK(*peers.security(again, handle) == Link::Open);
}

TEST_CASE("BLE link security: an open link asks once, pairing waits, and a failure is told once before asking again") {
    using Link = mm::platform::BleLinkSecurity;
    Link link = Link::Open; bool ask = false;
    CHECK(mm::platform::requestLinkSecurity(link, ask) == Link::Pairing); CHECK(ask);
    CHECK(mm::platform::requestLinkSecurity(link, ask) == Link::Pairing); CHECK_FALSE(ask);
    link = Link::Failed;
    CHECK(mm::platform::requestLinkSecurity(link, ask) == Link::Failed); CHECK_FALSE(ask);
    CHECK(mm::platform::requestLinkSecurity(link, ask) == Link::Pairing); CHECK(ask);
    link = Link::Encrypted;
    CHECK(mm::platform::requestLinkSecurity(link, ask) == Link::Encrypted); CHECK_FALSE(ask);
}
