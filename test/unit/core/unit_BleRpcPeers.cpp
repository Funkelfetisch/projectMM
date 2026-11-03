/// @module platform
#include "doctest.h"
#include "core/util/BleRpcPeers.h"
#include <cstring>

namespace {
constexpr const char* epoch = "0123456789abcdef0123456789abcdef";
constexpr const char* otherEpoch = "fedcba9876543210fedcba9876543210";
constexpr const char* nonceA = "11111111111111111111111111111111";
constexpr const char* nonceB = "22222222222222222222222222222222";
struct CountedSessions {
    static inline unsigned allocated = 0, freed = 0;
    static inline bool fail = false;
    static mm::BleRpcSession* create() {
        if (fail) return nullptr;
        auto* result = new (std::nothrow) mm::BleRpcSession();
        if (result) ++allocated;
        return result;
    }
    static void destroy(mm::BleRpcSession* session) { if (session) { ++freed; delete session; } }
};
}
TEST_CASE("BLE RPC peers: additional sessions allocate lazily and preserve active sessions on failure") {
    CountedSessions::allocated = CountedSessions::freed = 0; CountedSessions::fail = false;
    {
        mm::BleRpcPeers<8, CountedSessions> peers; REQUIRE(peers.beginEpoch(epoch));
        auto* a = peers.create(10); auto* b = peers.create(20); REQUIRE(a); REQUIRE(b);
        REQUIRE(a->hello(nonceA, nullptr, 0, peers.counter()) == nullptr);
        REQUIRE(b->hello(nonceB, nullptr, 0, peers.counter()) == nullptr);
        CHECK(CountedSessions::allocated == 0);
        CountedSessions::fail = true;
        CHECK_FALSE(peers.create(30)); CHECK(peers.find(10) == a); CHECK(peers.find(20) == b);
        CountedSessions::fail = false;
        auto* c = peers.create(30); REQUIRE(c); CHECK(CountedSessions::allocated == 1);
        REQUIRE(c->hello(nonceA, nullptr, 0, peers.counter()) == nullptr);
        const uint32_t id = c->session(); REQUIRE(peers.release(30));
        CHECK(peers.resume(40, epoch, id, nonceB) == c);
        CHECK(CountedSessions::freed == 0);
        REQUIRE(peers.beginEpoch(otherEpoch)); CHECK(CountedSessions::freed == 1);
        CHECK_FALSE(peers.find(10)); CHECK_FALSE(peers.findSession(id));
        REQUIRE(peers.create(1)); REQUIRE(peers.create(2)); REQUIRE(peers.create(3));
        CHECK(CountedSessions::allocated == 2);
    }
    CHECK(CountedSessions::freed == 2);
}
TEST_CASE("BLE RPC peers: allocation failure can reuse a disconnected session") {
    CountedSessions::allocated = CountedSessions::freed = 0; CountedSessions::fail = false;
    mm::BleRpcPeers<8, CountedSessions> peers; REQUIRE(peers.beginEpoch(epoch));
    auto* first = peers.create(10); auto* active = peers.create(20); REQUIRE(first); REQUIRE(active);
    REQUIRE(first->hello(nonceA, nullptr, 0, peers.counter()) == nullptr);
    REQUIRE(active->hello(nonceB, nullptr, 0, peers.counter()) == nullptr);
    const auto old = first->session(); REQUIRE(peers.release(10));
    CountedSessions::fail = true;
    CHECK(peers.create(30) == first); CHECK(peers.find(20) == active);
    CHECK_FALSE(peers.findSession(old)); CHECK(CountedSessions::allocated == 0);
    CountedSessions::fail = false;
}
TEST_CASE("BLE RPC peers: simultaneous sessions retain independent IDs and replies") {
    mm::BleRpcPeers<2> peers; REQUIRE(peers.beginEpoch(epoch));
    auto* a = peers.create(10); auto* b = peers.create(20);
    REQUIRE(a); REQUIRE(b); CHECK_FALSE(peers.create(30));
    REQUIRE(a->hello(nonceA, nullptr, 0, peers.counter()) == nullptr);
    REQUIRE(b->hello(nonceA, nullptr, 0, peers.counter()) == nullptr);
    CHECK(a->session() != b->session());
    REQUIRE(a->accept(epoch, a->session(), 1, "a") == mm::BleRpcSession::Decision::Accept);
    REQUIRE(b->accept(epoch, b->session(), 1, "b") == mm::BleRpcSession::Decision::Accept);
    REQUIRE(a->cacheReply("reply a")); REQUIRE(b->cacheReply("reply b"));
    REQUIRE(b->hello(nonceB, nullptr, 0, peers.counter()) == nullptr);
    CHECK(std::strcmp(a->cachedReply(), "reply a") == 0);
    CHECK(a->accept(epoch, a->session(), 1, "a") == mm::BleRpcSession::Decision::Retry);
}
TEST_CASE("BLE RPC peers: resume cannot steal an active peer and reuse expires retained state") {
    mm::BleRpcPeers<2> peers; REQUIRE(peers.beginEpoch(epoch));
    auto* a = peers.create(10); REQUIRE(a);
    REQUIRE(a->hello(nonceA, nullptr, 0, peers.counter()) == nullptr);
    const uint32_t id = a->session();
    REQUIRE(a->accept(epoch, id, 3, "retained") == mm::BleRpcSession::Decision::Accept);
    REQUIRE(a->cacheReply("result"));
    CHECK_FALSE(peers.resume(20, epoch, id, nonceB));
    REQUIRE(peers.release(10));
    CHECK_FALSE(peers.resume(20, otherEpoch, id, nonceB));
    CHECK_FALSE(peers.resume(20, epoch, id, "bad"));
    auto* resumed = peers.resume(20, epoch, id, nonceB); REQUIRE(resumed);
    CHECK(resumed->session() == id); CHECK(std::strcmp(resumed->cachedReply(), "result") == 0);
    REQUIRE(peers.release(20));
    auto* b = peers.create(30); REQUIRE(b);
    REQUIRE(b->hello(nonceA, nullptr, 0, peers.counter()) == nullptr);
    auto* c = peers.create(40); REQUIRE(c);
    REQUIRE(c->hello(nonceB, nullptr, 0, peers.counter()) == nullptr);
    CHECK_FALSE(peers.findSession(id));
    CHECK_FALSE(peers.resume(50, epoch, id, nonceA));
}

TEST_CASE("BLE RPC peers: reconciling a disconnect preserves the other live owner") {
    mm::BleRpcPeers<2> peers; REQUIRE(peers.beginEpoch(epoch));
    auto* a = peers.create(10); auto* b = peers.create(20); REQUIRE(a); REQUIRE(b);
    REQUIRE(a->hello(nonceA, nullptr, 0, peers.counter()) == nullptr);
    REQUIRE(b->hello(nonceB, nullptr, 0, peers.counter()) == nullptr);
    const uint32_t aSession = a->session(), bSession = b->session();
    const uint64_t live[] = {20}; peers.releaseInactive(live, 1);
    CHECK_FALSE(peers.find(10)); REQUIRE(peers.find(20));
    CHECK(peers.find(20)->session() == bSession);
    CHECK(peers.resume(30, epoch, aSession, nonceA) == a);
    CHECK(peers.find(20)->session() == bSession);
}

TEST_CASE("BLE RPC peers: many clients keep their results when another disconnects") {
    mm::BleRpcPeers<8> peers; REQUIRE(peers.beginEpoch(epoch));
    uint32_t sessions[8]{};
    for (unsigned i = 0; i < 8; ++i) {
        auto* peer = peers.create(i + 1); REQUIRE(peer);
        REQUIRE(peer->hello(nonceA, nullptr, 0, peers.counter()) == nullptr);
        sessions[i] = peer->session();
        REQUIRE(peer->accept(epoch, sessions[i], 1, "read") == mm::BleRpcSession::Decision::Accept);
        REQUIRE(peer->cacheReply(i == 3 ? "fourth" : "other"));
        for (unsigned j = 0; j < i; ++j) CHECK(sessions[j] != sessions[i]);
    }
    CHECK_FALSE(peers.create(9));
    const uint64_t live[] = {1, 2, 3, 5, 6, 7, 8};
    peers.releaseInactive(live, 7);
    auto* resumed = peers.resume(9, epoch, sessions[3], nonceB); REQUIRE(resumed);
    CHECK(std::strcmp(resumed->cachedReply(), "fourth") == 0);
    for (unsigned i = 0; i < 8; ++i) if (i != 3) {
        auto* peer = peers.find(i + 1); REQUIRE(peer);
        CHECK(peer->session() == sessions[i]);
        CHECK(peer->accept(epoch, sessions[i], 1, "read") == mm::BleRpcSession::Decision::Retry);
        CHECK(std::strcmp(peer->cachedReply(), "other") == 0);
    }
}
