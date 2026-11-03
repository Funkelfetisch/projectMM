/// Epoch/session correlation and exact retained retries for BLE RPC reads and writes.
#include "doctest.h"
#include "core/util/BleRpcSession.h"

#include <array>
#include <cstdint>
#include <string>
#include <type_traits>

namespace {
constexpr const char* kEpoch = "0123456789abcdef0123456789abcdef";
constexpr const char* kNextEpoch = "fedcba9876543210fedcba9876543210";
constexpr const char* kClient = "11111111111111111111111111111111";
constexpr const char* kNextClient = "22222222222222222222222222222222";
using Decision = mm::BleRpcSession::Decision;

void start(mm::BleRpcSession& session) {
    REQUIRE(session.beginEpoch(kEpoch));
    REQUIRE(session.hello(kClient) == nullptr);
}
}

static_assert(std::is_trivially_destructible_v<mm::BleRpcSession>);

TEST_CASE("BLE RPC sessions: shared epoch allocation gives independent peers unique namespaces") {
    mm::BleRpcSession first, second;
    uint32_t counter = 0;
    REQUIRE(first.beginEpoch(kEpoch)); REQUIRE(second.beginEpoch(kEpoch));
    REQUIRE(first.hello(kClient, nullptr, 0, &counter) == nullptr);
    REQUIRE(second.hello(kClient, nullptr, 0, &counter) == nullptr);
    CHECK(first.session() == 1); CHECK(second.session() == 2); CHECK(counter == 2);
    REQUIRE(first.accept(kEpoch, 1, 1, "first") == Decision::Accept);
    REQUIRE(first.cacheReply("first reply"));
    REQUIRE(second.accept(kEpoch, 2, 1, "second") == Decision::Accept);
    REQUIRE(second.cacheReply("second reply"));
    REQUIRE(first.hello(kClient, nullptr, 0, &counter) == nullptr);
    CHECK(counter == 2); CHECK(std::string(first.cachedReply()) == "first reply");
    REQUIRE(second.hello(kNextClient, nullptr, 0, &counter) == nullptr);
    CHECK(second.session() == 3); CHECK(first.session() == 1);
    CHECK(first.accept(kEpoch, 1, 1, "first") == Decision::Retry);
    CHECK(std::string(first.cachedReply()) == "first reply");
    counter = UINT32_MAX;
    CHECK(std::string(first.hello(kNextClient, nullptr, 0, &counter)) == "session-exhausted");
    CHECK(first.session() == 1); CHECK(first.highWater() == 1);
}

TEST_CASE("BLE RPC sessions: a new central's same-ID reads belong to a fresh namespace") {
    for (const char* operation : {"get", "clock", "battery"}) {
        mm::BleRpcSession session; start(session);
        const std::string request = std::string("{\"$id\":1,\"$op\":\"") + operation + "\"}";
        const uint32_t oldSession = session.session();
        REQUIRE(session.accept(kEpoch, oldSession, 1, request.c_str()) == Decision::Accept);
        REQUIRE(session.cacheReply(R"({"id":1,"now":10,"ok":true})"));
        REQUIRE(session.hello(kNextClient) == nullptr);
        CHECK(session.session() == oldSession + 1);
        CHECK(session.highWater() == 0);
        CHECK(std::string(session.cachedReply()).empty());
        CHECK(session.accept(kEpoch, oldSession, 1, request.c_str()) == Decision::SessionExpired);
        CHECK(session.accept(kEpoch, session.session(), 1, request.c_str()) == Decision::Accept);
        CHECK(std::string(session.cachedReply()).empty());
    }
}

TEST_CASE("BLE RPC sessions: retries retain exact bytes and expired IDs never execute again") {
    mm::BleRpcSession session; start(session);
    const char* request = R"({"$id":5,"$op":"patch","Drivers":{"brightness":32}})";
    REQUIRE(session.accept(kEpoch, session.session(), 5, request) == Decision::Accept);
    REQUIRE(session.cacheReply(R"({"id":5,"ok":true,"brightness":32})"));
    CHECK(session.accept(kEpoch, session.session(), 5, request) == Decision::Retry);
    CHECK(std::string(session.cachedReply()) == R"({"id":5,"ok":true,"brightness":32})");
    CHECK(session.accept(kEpoch, session.session(), 5, R"({"$id":5,"$op":"patch","Drivers":{"brightness":33}})") == Decision::IdReused);
    CHECK(session.highWater() == 5);
    CHECK(session.accept(kEpoch, session.session(), 4, "older") == Decision::IdExpired);
    CHECK(session.accept(kEpoch, session.session(), 6, "next") == Decision::Accept);
    CHECK(std::string(session.cachedReply()).empty());
    CHECK(session.accept(kEpoch, session.session(), 5, request) == Decision::IdExpired);
}

TEST_CASE("BLE RPC sessions: hello retry and explicit resume preserve cache and request high-water") {
    mm::BleRpcSession session; start(session);
    const uint32_t original = session.session();
    REQUIRE(session.accept(kEpoch, original, 17, "request") == Decision::Accept);
    REQUIRE(session.cacheReply("result"));
    REQUIRE(session.hello(kClient) == nullptr);
    CHECK(session.session() == original);
    CHECK(session.highWater() == 17);
    CHECK(std::string(session.cachedReply()) == "result");
    REQUIRE(session.hello(kNextClient, kEpoch, original) == nullptr);
    CHECK(session.session() == original);
    CHECK(std::string(session.nonce()) == kNextClient);
    CHECK(session.accept(kEpoch, original, 17, "request") == Decision::Retry);
    CHECK(std::string(session.cachedReply()) == "result");
    CHECK(std::string(session.hello(kClient, kNextEpoch, original)) == "session-expired");
    CHECK(std::string(session.hello(kClient, kEpoch, original + 1)) == "session-expired");
    CHECK(std::string(session.hello(kClient, nullptr, original)) == "session-expired");
    CHECK(std::string(session.hello(kClient, kEpoch, 0)) == "session-expired");
    CHECK(session.session() == original);
    CHECK(session.highWater() == 17);
    CHECK(std::string(session.nonce()) == kNextClient);
}

TEST_CASE("BLE RPC sessions: service epochs invalidate old sessions and maximum request IDs require a new hello") {
    mm::BleRpcSession session; start(session);
    REQUIRE(session.accept(kEpoch, session.session(), UINT16_MAX, "last") == Decision::Accept);
    REQUIRE(session.cacheReply("last result"));
    CHECK(session.accept(kEpoch, session.session(), 1, "wrapped") == Decision::IdExpired);
    const uint32_t original = session.session();
    REQUIRE(session.hello(kNextClient) == nullptr);
    CHECK(session.session() == original + 1);
    CHECK(session.accept(kEpoch, session.session(), 1, "fresh") == Decision::Accept);
    REQUIRE(session.beginEpoch(kNextEpoch));
    CHECK(std::string(session.epoch()) == kNextEpoch);
    CHECK(session.session() == 0);
    CHECK(session.highWater() == 0);
    CHECK(std::string(session.nonce()).empty());
    CHECK(std::string(session.cachedReply()).empty());
    CHECK(session.accept(kEpoch, original, 1, "old") == Decision::SessionExpired);
    CHECK(std::string(session.hello(kClient, kEpoch, original)) == "session-expired");
    REQUIRE(session.hello(kClient) == nullptr);
    CHECK(session.session() == 1);
    CHECK(session.accept(kNextEpoch, 1, 1, "new epoch") == Decision::Accept);
}

TEST_CASE("BLE RPC sessions: malformed tokens and request bodies leave accepted state intact") {
    mm::BleRpcSession session;
    CHECK(std::string(session.hello(kClient)) == "epoch-unavailable");
    start(session);
    REQUIRE(session.accept(kEpoch, session.session(), 9, "retained") == Decision::Accept);
    REQUIRE(session.cacheReply("retained reply"));
    for (const char* invalid : std::array<const char*, 5>{nullptr, "", "0", "0123456789abcdef0123456789abcdeF", "0123456789abcdef0123456789abcdef0"}) {
        CHECK_FALSE(session.beginEpoch(invalid));
        CHECK(std::string(session.epoch()) == kEpoch);
        CHECK(std::string(session.hello(invalid)) == "nonce");
        CHECK(session.accept(invalid, session.session(), 10, "request") == Decision::Invalid);
    }
    const std::string oversized(mm::BleMessage::kCapacity + 1, 'x');
    for (const char* invalid : std::array<const char*, 3>{nullptr, "", oversized.c_str()})
        CHECK(session.accept(kEpoch, session.session(), 10, invalid) == Decision::Invalid);
    CHECK(session.accept(kEpoch, session.session(), 0, "request") == Decision::Invalid);
    CHECK(session.accept(kNextEpoch, session.session(), 10, "request") == Decision::SessionExpired);
    CHECK(session.accept(kEpoch, 0, 10, "request") == Decision::SessionExpired);
    CHECK(session.highWater() == 9);
    CHECK(std::string(session.cachedReply()) == "retained reply");
    CHECK(session.accept(kEpoch, session.session(), 9, "retained") == Decision::Retry);
}

TEST_CASE("BLE RPC sessions: bounded request and result caches preserve their exact maximum-size contents") {
    mm::BleRpcSession session; start(session);
    const std::string request(mm::BleMessage::kCapacity, 'q');
    const std::string reply(mm::BleMessage::kCapacity, 'r');
    const std::string oversized(mm::BleMessage::kCapacity + 1, 'x');
    REQUIRE(session.accept(kEpoch, session.session(), 1, request.c_str()) == Decision::Accept);
    REQUIRE(session.cacheReply(reply.c_str()));
    REQUIRE(session.cacheReply(session.cachedReply()));
    CHECK(std::string(session.cachedReply()) == reply);
    CHECK(session.accept(kEpoch, session.session(), 1, request.c_str()) == Decision::Retry);
    CHECK_FALSE(session.cacheReply(nullptr));
    CHECK_FALSE(session.cacheReply(""));
    CHECK_FALSE(session.cacheReply(oversized.c_str()));
    CHECK(std::string(session.cachedReply()) == reply);
}
