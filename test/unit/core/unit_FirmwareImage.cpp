/// Reading a firmware image's own header, which is what stands between a mistyped URL and a board with no recovery image.
///
/// @technical
///
/// The MoonBase update ERASES the factory partition before it can know whether a stream is any good, so everything that can reject an image is decided from its first chunk.
/// These tests are that decision.
///
/// ## Real binaries, not only synthetic ones
///
/// The offsets are pinned against REAL binaries wherever the build tree has them.
/// The layout comes from the ESP32 toolchain, not from us, and a test that only checks our own synthetic bytes would agree with itself while disagreeing with every actual image.
/// Every image found is checked, and a found image that cannot be parsed FAILS rather than being skipped: skipping the unparseable ones is skipping exactly what this catches.
///
/// ## Empty means not built, not passed
///
/// A checkout that has not built for ESP32 has no images to check.
/// That case has to stay distinguishable from "present but not checked".
/// A loop that skips missing files and asserts nothing reports green either way, which looks exactly like a test that ran.

#include "doctest.h"

#include "core/util/FirmwareImage.h"
#include "core/util/FirmwareUploadSession.h"
#include "core/util/FirmwareUploadBackend.h"
#include "core/util/FirmwareUpdateGate.h"
#include "core/util/FirmwareOtaPort.h"
#include "core/util/FirmwareUploadFrame.h"
#include "core/util/FirmwareUploadProtocol.h"
#include "core/util/FirmwareUploadCoordinator.h"
#include "core/util/FirmwareUploadAssembly.h"
#include "core/util/FirmwareUploadFragmentBridge.h"
#include "core/util/FirmwareUploadFragmentPacket.h"
#include "core/util/sha256.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <vector>
#include <atomic>
#include <thread>
#include <utility>

using namespace mm::firmware;

TEST_CASE("firmware frame assembly handles small transport payloads and ownership") {
    using A = mm::FirmwareUploadAssembly;
    A a(30);
    std::array<uint8_t, 54> frame{};
    frame[0] = 1; frame[1] = 1; frame[2] = 4; frame[38] = 1;
    CHECK(a.append(0, frame.size(), 0, frame.data(), 20, 100) == A::Result::Invalid);
    CHECK(a.append(1, frame.size(), 1, frame.data(), 20, 100) == A::Result::Offset);
    REQUIRE(a.append(1, frame.size(), 0, frame.data(), 20, 100) == A::Result::Pending);
    CHECK(a.data() == nullptr);
    CHECK(a.append(2, frame.size(), 20, frame.data() + 20, 20, 101) == A::Result::Owner);
    CHECK(a.append(1, frame.size(), 0, frame.data(), 20, 102) == A::Result::Offset);
    CHECK(a.append(1, 53, 20, frame.data() + 20, 20, 102) == A::Result::Offset);
    REQUIRE(a.append(1, frame.size(), 20, frame.data() + 20, 20, 103) == A::Result::Pending);
    REQUIRE(a.append(1, frame.size(), 40, frame.data() + 40, 14, 104) == A::Result::Ready);
    mm::FirmwareUploadFrame parsed;
    CHECK(mm::FirmwareUploadFrame::parse(a.data(), a.size(), parsed));
    CHECK(parsed.kind == mm::FirmwareUploadFrame::Kind::Begin);
    CHECK(parsed.token[0] == 1);
    CHECK(a.append(1, frame.size(), 0, frame.data(), 20, 105) == A::Result::Busy);
    a.release(2); CHECK(a.data() != nullptr);
    a.release(1); CHECK(a.data() == nullptr);
    CHECK(a.append(1, 513, 0, frame.data(), 20, 106) == A::Result::Invalid);
    REQUIRE(a.append(3, frame.size(), 0, frame.data(), 20, 0xfffffff0u) == A::Result::Pending);
    CHECK_FALSE(a.expire(13)); CHECK(a.expire(14));
    CHECK(a.append(3, frame.size(), 20, frame.data() + 20, 20, 15) == A::Result::Offset);
}

TEST_CASE("firmware upload frames validate version, lengths and chunk offsets") {
    using F = mm::FirmwareUploadFrame; F parsed;
    std::array<uint8_t, 38> begin{}; begin[0] = 1; begin[1] = 1;
    begin[2] = 0x78; begin[3] = 0x56; begin[4] = 0x34; begin[5] = 0x12; begin[37] = 0xaa;
    REQUIRE(F::parse(begin.data(), begin.size(), parsed)); CHECK(parsed.kind == F::Kind::Begin);
    CHECK(parsed.value == 0x12345678); CHECK(parsed.digest[31] == 0xaa);
    for (size_t n = 0; n < begin.size(); ++n) CHECK_FALSE(F::parse(begin.data(), n, parsed));
    CHECK_FALSE(F::parse(nullptr, 38, parsed)); CHECK(parsed.data == nullptr); CHECK(parsed.length == 0);
    begin[0] = 2; CHECK_FALSE(F::parse(begin.data(), begin.size(), parsed)); begin[0] = 1;
    begin[2] = begin[3] = begin[4] = begin[5] = 0; CHECK_FALSE(F::parse(begin.data(), begin.size(), parsed));
    std::array<uint8_t, 513> chunk{}; chunk[0] = 1; chunk[1] = 2; chunk[2] = 0x34; chunk[3] = 0x12;
    CHECK_FALSE(F::parse(chunk.data(), 6, parsed));
    REQUIRE(F::parse(chunk.data(), 512, parsed)); CHECK(parsed.kind == F::Kind::Chunk);
    CHECK(parsed.value == 0x1234); CHECK(parsed.length == 506); CHECK(parsed.data == chunk.data() + 6);
    CHECK_FALSE(F::parse(chunk.data(), chunk.size(), parsed));
    for (uint8_t op : {uint8_t(3), uint8_t(4), uint8_t(5)}) {
        const uint8_t simple[] = {1, op, 0}; REQUIRE(F::parse(simple, 2, parsed));
        CHECK_FALSE(F::parse(simple, 3, parsed));
    }
    const uint8_t unknown[] = {1, 6}; CHECK_FALSE(F::parse(unknown, sizeof(unknown), parsed));
}

namespace {
struct UploadBackend {
    std::vector<uint8_t> data;
    bool failBegin = false, failWrite = false, failCommit = false;
    unsigned aborts = 0, commits = 0; size_t limit = 8;
    size_t capacity() const { return limit; }
    bool begin(size_t) { data.clear(); return !failBegin; }
    bool write(const uint8_t* p, size_t n) { if (failWrite) return false; data.insert(data.end(), p, p + n); return true; }
    bool end(bool commit) { if (commit) { ++commits; return !failCommit; } ++aborts; return true; }
};
struct FakePlatformOta {
    static inline unsigned begins = 0, commits = 0, aborts = 0;
    static inline bool failWrite = false;
    static inline uint32_t* written = nullptr;
    static inline int handle = 0;
    static size_t capacity() { return 8; }
    static bool begin(mm::platform::OtaWrite& w, size_t n, char* status, size_t cap, uint32_t* count) {
        if (w.impl || n > capacity()) return false;
        ++begins; w.impl = &handle; written = count; *count = 0; std::snprintf(status, cap, "ready"); return true;
    }
    static bool write(mm::platform::OtaWrite& w, const uint8_t*, size_t n) {
        if (failWrite) { w.impl = nullptr; return false; }
        *written += static_cast<uint32_t>(n); return true;
    }
    static bool end(mm::platform::OtaWrite& w, bool commit) {
        if (!w.impl) return false;
        w.impl = nullptr; if (commit) ++commits; else ++aborts; return commit;
    }
};
struct FakeAlreadyGatedOta {
    static inline mm::FirmwareUpdateGate gate;
    static inline int activeHandle = 0;
    static size_t capacity() { return 32; }
    static bool begin(mm::platform::OtaWrite& w, size_t n, char*, size_t, uint32_t* count) {
        if (!n || n > capacity() || !gate.acquire()) return false;
        w.impl = &activeHandle; *count = 0; return true;
    }
    static bool write(mm::platform::OtaWrite& w, const uint8_t*, size_t) { return w.impl != nullptr; }
    static bool end(mm::platform::OtaWrite& w, bool) {
        if (!w.impl) return false;
        w.impl = nullptr; gate.release(); return true;
    }
};
}

TEST_CASE("experimental upload packet validates lengths and preserves assembly on bad input") {
    using P = mm::FirmwareUploadFragmentPacket;
    P parsed;
    const uint8_t first[] = {2, 0, 0, 0, 1};
    const uint8_t last[] = {2, 0, 1, 0, 5}; // status frame
    CHECK(P::parse(first, sizeof(first), parsed));
    CHECK(parsed.total == 2); CHECK(parsed.offset == 0); CHECK(parsed.length == 1);
    const uint8_t invalid[][5] = {{0,0,0,0,1}, {1,0,0,0,1}, {1,2,0,0,1}, {2,0,2,0,1}};
    CHECK_FALSE(P::parse(nullptr, 5, parsed));
    CHECK_FALSE(P::parse(first, 4, parsed));
    for (auto& packet : invalid) CHECK_FALSE(P::parse(packet, sizeof(packet), parsed));
    CHECK(parsed.data == nullptr);
    std::array<uint8_t, 513> oversized{};
    oversized[1] = 2;
    CHECK_FALSE(P::parse(oversized.data(), oversized.size(), parsed));
    std::array<uint8_t, 512> maximum{};
    maximum[1] = 2;
    CHECK(P::parse(maximum.data(), maximum.size(), parsed));
    CHECK(parsed.length == 508);
    UploadBackend backend;
    mm::FirmwareUploadFragmentBridge bridge(backend, 100, 100, 100);
    const auto pending = mm::appendFirmwareUploadPacket(bridge, 1, first, sizeof(first), 1);
    CHECK_FALSE(pending.dispatched);
    const auto bad = mm::appendFirmwareUploadPacket(bridge, 1, invalid[3], sizeof(invalid[3]), 2);
    CHECK(bad.assembly == decltype(bridge)::AssemblyResult::Invalid);
    const auto complete = mm::appendFirmwareUploadPacket(bridge, 1, last, sizeof(last), 3);
    CHECK(complete.dispatched);
    CHECK(complete.outcome.result == decltype(bridge)::Coordinator::Result::Owner);
}

TEST_CASE("fragment bridge dispatches complete frames and preserves upload ownership") {
    UploadBackend backend;
    mm::FirmwareUploadFragmentBridge bridge(backend, 10, 20, 30);
    using B = decltype(bridge); using A = B::AssemblyResult;
    using R = B::Coordinator::Result;
    const uint8_t image[] = {1,2,3,4}; std::array<uint8_t, 32> digest{};
    mm::sha256(image, sizeof(image), digest.data());
    uint8_t begin[54] = {1,1,4,0,0,0}; std::memcpy(begin + 6, digest.data(), digest.size()); begin[38] = 7;
    const uint8_t chunk[] = {1,2,0,0,0,0,1,2,3,4};
    const uint8_t resume[18] = {1,6,7}, finish[] = {1,3};
    CHECK(bridge.append(11,54,0,begin,20,100).assembly == A::Pending);
    CHECK(bridge.append(12,54,20,begin+20,20,101).assembly == A::Owner);
    CHECK(bridge.append(11,54,21,begin+20,20,101).assembly == A::Offset);
    CHECK(bridge.append(11,54,20,begin+20,20,102).assembly == A::Pending);
    const auto started = bridge.append(11,54,40,begin+40,14,103);
    REQUIRE(started.dispatched); CHECK(started.outcome.result == R::Ok); CHECK(bridge.active());
    auto uploaded = bridge.append(11,sizeof(chunk),0,chunk,sizeof(chunk),104);
    REQUIRE(uploaded.dispatched); CHECK(uploaded.outcome.result == R::Ok);
    CHECK(uploaded.outcome.hasStatus); CHECK(uploaded.outcome.status.received == 4);
    CHECK(bridge.append(11,2,0,finish,1,105).assembly == A::Pending);
    REQUIRE(bridge.disconnected(11,106) == R::Ok);
    CHECK(bridge.append(12,2,1,finish+1,1,107).assembly == A::Offset);
    REQUIRE(bridge.append(12,sizeof(resume),0,resume,sizeof(resume),108).outcome.result == R::Ok);
    auto committed = bridge.append(12,sizeof(finish),0,finish,sizeof(finish),109);
    REQUIRE(committed.dispatched); CHECK(committed.outcome.result == R::Ok);
    CHECK(committed.outcome.hasStatus); CHECK(committed.outcome.status.phase == B::Coordinator::Session::Phase::Committed);
    CHECK(backend.commits == 1); CHECK(backend.data == std::vector<uint8_t>(image,image+sizeof(image)));
    CHECK(bridge.append(12,sizeof(finish),0,finish,sizeof(finish),110).outcome.result == R::Owner);
    CHECK(backend.commits == 1);
    CHECK(bridge.append(13,54,0,begin,20,200).assembly == A::Pending);
    bridge.tick(210);
    CHECK(bridge.append(13,54,20,begin+20,20,210).assembly == A::Offset);
}

TEST_CASE("firmware upload coordinator expires before processing and handles reconnect") {
    UploadBackend backend;
    mm::FirmwareUploadCoordinator coordinator(backend, 20, 30);
    using C = decltype(coordinator); using R = C::Result;
    const uint8_t image[] = {1,2,3,4}; std::array<uint8_t, 32> digest{};
    mm::sha256(image, sizeof(image), digest.data());
    uint8_t begin[54] = {1,1,4,0,0,0}; std::memcpy(begin + 6, digest.data(), digest.size()); begin[38] = 7;
    const uint8_t first[] = {1,2,0,0,0,0,1,2}, second[] = {1,2,2,0,0,0,3,4};
    const uint8_t resume[18] = {1,6,7}, finish[] = {1,3}, status[] = {1,5};
    REQUIRE(coordinator.handle(11, begin, sizeof(begin), 100) == R::Ok);
    REQUIRE(coordinator.handle(11, first, sizeof(first), 101) == R::Ok);
    REQUIRE(coordinator.disconnected(11, 105) == R::Ok);
    CHECK_FALSE(coordinator.tick(134));
    REQUIRE(coordinator.handle(12, resume, sizeof(resume), 134) == R::Ok);
    REQUIRE(coordinator.handle(12, second, sizeof(second), 135) == R::Ok);
    REQUIRE(coordinator.handle(12, finish, sizeof(finish), 136) == R::Ok);
    const uint8_t tokenStatus[18] = {1,5,7};
    const auto terminal = coordinator.handleAndReport(13, tokenStatus, sizeof(tokenStatus), 137);
    CHECK(terminal.result == R::Ok); CHECK(terminal.hasStatus);
    CHECK(terminal.status.phase == C::Session::Phase::Committed); CHECK(terminal.status.received == 4);
    const uint8_t wrongToken[18] = {1,5,8};
    const auto denied = coordinator.handleAndReport(13, wrongToken, sizeof(wrongToken), 137);
    CHECK(denied.result == R::Owner); CHECK_FALSE(denied.hasStatus);
    C::Status observed{};
    REQUIRE(coordinator.handle(12, status, sizeof(status), 137, &observed) == R::Ok);
    CHECK(observed.phase == C::Session::Phase::Committed); CHECK(observed.received == 4);
    CHECK(backend.commits == 1); CHECK(backend.aborts == 0);

    REQUIRE(coordinator.handle(11, begin, sizeof(begin), 200) == R::Ok);
    CHECK(coordinator.handle(11, first, sizeof(first), 220) == R::Owner);
    CHECK_FALSE(coordinator.active()); CHECK(backend.aborts == 1);
    REQUIRE(coordinator.handle(11, begin, sizeof(begin), 230) == R::Ok);
    REQUIRE(coordinator.disconnected(11, 231) == R::Ok);
    CHECK(coordinator.handle(12, resume, sizeof(resume), 261) == R::Owner);
    CHECK_FALSE(coordinator.active()); CHECK(backend.aborts == 2);
    REQUIRE(coordinator.handle(12, begin, sizeof(begin), 262) == R::Ok);
}

TEST_CASE("firmware upload protocol: complete framed transfer retains owner and rejects replay") {
    UploadBackend backend; mm::FirmwareUploadSession session(backend); using R = decltype(session)::Result;
    const uint8_t image[] = {1,2,3,4}; std::array<uint8_t, 32> digest{}; mm::sha256(image, sizeof(image), digest.data());
    uint8_t begin[38] = {1, 1, 4, 0, 0, 0}; std::memcpy(begin + 6, digest.data(), digest.size());
    const uint8_t first[] = {1, 2, 0, 0, 0, 0, 1, 2};
    const uint8_t second[] = {1, 2, 2, 0, 0, 0, 3, 4};
    const uint8_t finish[] = {1, 3}, status[] = {1, 5};
    REQUIRE(mm::dispatchFirmwareUpload(session, 11, begin, sizeof(begin), 100) == R::Ok);
    CHECK(mm::dispatchFirmwareUpload(session, 12, status, sizeof(status), 100) == R::Owner);
    CHECK(mm::dispatchFirmwareUpload(session, 12, first, sizeof(first), 100) == R::Owner);
    REQUIRE(mm::dispatchFirmwareUpload(session, 11, first, sizeof(first), 100) == R::Ok);
    CHECK(mm::dispatchFirmwareUpload(session, 11, first, sizeof(first), 100) == R::Offset);
    CHECK(mm::dispatchFirmwareUpload(session, 11, status, sizeof(status), 100) == R::Ok); CHECK(session.received() == 2);
    REQUIRE(mm::dispatchFirmwareUpload(session, 11, second, sizeof(second), 100) == R::Ok);
    CHECK(mm::dispatchFirmwareUpload(session, 11, finish, sizeof(finish), 100) == R::Ok);
    CHECK(backend.commits == 1); CHECK(backend.data == std::vector<uint8_t>(image, image + sizeof(image)));
    CHECK(mm::dispatchFirmwareUpload(session, 11, finish, sizeof(finish), 100) == R::Owner);
    CHECK(mm::dispatchFirmwareUpload(session, 11, status, sizeof(status), 100) == R::Ok);
    CHECK(mm::dispatchFirmwareUpload(session, 12, status, sizeof(status), 100) == R::Owner);
    decltype(session)::Status outcome{}; REQUIRE(session.status(11, outcome));
    CHECK(outcome.phase == decltype(session)::Phase::Committed); CHECK(outcome.result == R::Ok);
    CHECK(outcome.received == 4); CHECK(outcome.total == 4); CHECK(backend.commits == 1);
}

TEST_CASE("firmware upload protocol: connected stalled transfers release their update slot") {
    UploadBackend backend; mm::FirmwareUploadSession session(backend);
    using S = decltype(session); using R = S::Result;
    const uint8_t image[] = {1,2,3,4}; std::array<uint8_t, 32> digest{};
    mm::sha256(image, sizeof(image), digest.data());
    uint8_t begin[38] = {1,1,4,0,0,0}; std::memcpy(begin + 6, digest.data(), digest.size());
    const uint8_t first[] = {1,2,0,0,0,0,1,2}, status[] = {1,5};
    REQUIRE(mm::dispatchFirmwareUpload(session, 11, begin, sizeof(begin), 0xfffffff0u) == R::Ok);
    CHECK_FALSE(session.expireIdle(50, 0));
    CHECK_FALSE(session.expireIdle(50, 0x80000000u));
    REQUIRE(mm::dispatchFirmwareUpload(session, 11, first, sizeof(first), 0xfffffffau) == R::Ok);
    CHECK(mm::dispatchFirmwareUpload(session, 11, first, sizeof(first), 12) == R::Offset);
    CHECK(mm::dispatchFirmwareUpload(session, 11, status, sizeof(status), 13) == R::Ok);
    CHECK_FALSE(session.expireIdle(13, 20));
    CHECK(session.expireIdle(14, 20));
    CHECK_FALSE(session.active()); CHECK(backend.aborts == 1); CHECK(backend.commits == 0);
    S::Status result{}; REQUIRE(session.status(11, result));
    CHECK(result.phase == S::Phase::Aborted); CHECK(result.received == 2); CHECK(result.total == 4);
    REQUIRE(mm::dispatchFirmwareUpload(session, 12, begin, sizeof(begin), 100) == R::Ok);
    CHECK_FALSE(session.expireIdle(119, 20)); CHECK(session.expireIdle(120, 20));
    CHECK(backend.aborts == 2);

    uint8_t resumable[54] = {1,1,4,0,0,0};
    std::memcpy(resumable + 6, digest.data(), digest.size()); resumable[38] = 7;
    uint8_t resume[18] = {1,6,7};
    REQUIRE(mm::dispatchFirmwareUpload(session, 11, resumable, sizeof(resumable), 200) == R::Ok);
    REQUIRE(session.disconnected(11, 205, 50) == R::Ok);
    CHECK_FALSE(session.expireIdle(230, 20)); // suspension uses its own deadline
    REQUIRE(mm::dispatchFirmwareUpload(session, 12, resume, sizeof(resume), 230) == R::Ok);
    CHECK_FALSE(session.expireIdle(249, 20)); CHECK(session.expireIdle(250, 20));
    CHECK(backend.aborts == 3);
}

TEST_CASE("firmware upload protocol: framed reconnect and terminal correlation") {
    UploadBackend backend; mm::FirmwareUploadSession session(backend); using S = decltype(session); using R = S::Result;
    const uint8_t image[] = {1,2,3,4}; std::array<uint8_t, 32> digest{}; mm::sha256(image, sizeof(image), digest.data());
    uint8_t begin[54] = {1, 1, 4, 0, 0, 0}; std::memcpy(begin + 6, digest.data(), digest.size()); begin[38] = 42;
    const uint8_t first[] = {1,2,0,0,0,0,1,2}, second[] = {1,2,2,0,0,0,3,4}, finish[] = {1,3};
    uint8_t resume[18] = {1,6,42}, wrong[18] = {1,6,43}, status[18] = {1,5,42};
    mm::FirmwareUploadFrame parsed;
    for (size_t n = 39; n < 54; ++n) CHECK_FALSE(mm::FirmwareUploadFrame::parse(begin, n, parsed));
    uint8_t zero[18] = {1,6}; CHECK_FALSE(mm::FirmwareUploadFrame::parse(zero, sizeof(zero), parsed));
    begin[38] = 0; CHECK_FALSE(mm::FirmwareUploadFrame::parse(begin, sizeof(begin), parsed)); begin[38] = 42;
    REQUIRE(mm::dispatchFirmwareUpload(session, 11, begin, sizeof(begin), 100) == R::Ok);
    REQUIRE(mm::dispatchFirmwareUpload(session, 11, first, sizeof(first), 100) == R::Ok);
    const uint8_t current[] = {1,5}; S::Status observed{S::Phase::Failed, R::Io, 9, 9};
    REQUIRE(mm::dispatchFirmwareUpload(session, 11, current, sizeof(current), 100, &observed) == R::Ok);
    CHECK(observed.phase == S::Phase::Uploading); CHECK(observed.received == 2); CHECK(observed.total == 4);
    REQUIRE(session.suspend(11, 100, 30) == R::Ok);
    CHECK(mm::dispatchFirmwareUpload(session, 12, wrong, sizeof(wrong), 110) == R::Owner);
    REQUIRE(mm::dispatchFirmwareUpload(session, 12, resume, sizeof(resume), 110) == R::Ok);
    REQUIRE(mm::dispatchFirmwareUpload(session, 12, second, sizeof(second), 110) == R::Ok);
    REQUIRE(mm::dispatchFirmwareUpload(session, 12, finish, sizeof(finish), 110) == R::Ok);
    CHECK(mm::dispatchFirmwareUpload(session, 0, status, sizeof(status), 110) == R::Owner);
    REQUIRE(mm::dispatchFirmwareUpload(session, 13, status, sizeof(status), 110, &observed) == R::Ok);
    CHECK(observed.phase == S::Phase::Committed); CHECK(observed.result == R::Ok);
    CHECK(observed.received == 4); CHECK(observed.total == 4);
    status[2] = 43; observed = {S::Phase::Failed, R::Io, 9, 9};
    CHECK(mm::dispatchFirmwareUpload(session, 13, status, sizeof(status), 110, &observed) == R::Owner);
    CHECK(observed.phase == S::Phase::Failed); CHECK(observed.received == 9);
    CHECK(backend.commits == 1); CHECK(backend.data == std::vector<uint8_t>(image, image + sizeof(image)));
}

TEST_CASE("firmware upload session: disconnect safely suspends or releases its slot") {
    UploadBackend backend; mm::FirmwareUploadSession session(backend);
    using S = decltype(session); using R = S::Result;
    std::array<uint8_t, 32> digest{}; std::array<uint8_t, 16> token{}; token[0] = 7;
    REQUIRE(session.begin(11, 4, digest) == R::Ok);
    CHECK(session.disconnected(12, 100, 20) == R::Owner); CHECK(session.active());
    REQUIRE(session.disconnected(11, 100, 20) == R::Ok);
    CHECK_FALSE(session.active()); CHECK(backend.aborts == 1);
    S::Status result{}; REQUIRE(session.status(11, result)); CHECK(result.phase == S::Phase::Aborted);
    REQUIRE(session.begin(11, 4, digest, token) == R::Ok);
    REQUIRE(session.disconnected(11, 0xfffffff0u, 30) == R::Ok);
    CHECK(session.active()); CHECK(backend.aborts == 1);
    CHECK_FALSE(session.expire(13)); CHECK(session.expire(14));
    CHECK_FALSE(session.active()); CHECK(backend.aborts == 2);
    REQUIRE(session.begin(11, 4, digest, token) == R::Ok);
    CHECK(session.disconnected(11, 0, 0) == R::Invalid);
    CHECK_FALSE(session.active()); CHECK(backend.aborts == 3);
    REQUIRE(session.terminalStatus(token, result)); CHECK(result.result == R::Invalid);
}

TEST_CASE("firmware upload session: ordered owned chunks commit only after the complete verified image") {
    UploadBackend backend; mm::FirmwareUploadSession<UploadBackend> session(backend);
    using R = decltype(session)::Result;
    const uint8_t bytes[] = {1,2,3,4}; std::array<uint8_t, 32> digest{}; mm::sha256(bytes, sizeof(bytes), digest.data());
    CHECK(session.begin(0, 4, digest) == R::Invalid); CHECK(session.begin(1, 9, digest) == R::Invalid);
    REQUIRE(session.begin(1, 4, digest) == R::Ok); CHECK(session.begin(2, 4, digest) == R::Busy);
    CHECK(session.append(2, 0, bytes, 2) == R::Owner); CHECK(session.received() == 0);
    REQUIRE(session.append(1, 0, bytes, 2) == R::Ok);
    CHECK(session.append(1, 0, bytes, 2) == R::Offset); CHECK(session.append(1, 2, bytes, 3) == R::Invalid);
    CHECK(session.finish(1) == R::Incomplete); CHECK(backend.commits == 0);
    REQUIRE(session.append(1, 2, bytes + 2, 2) == R::Ok); CHECK(session.finish(1) == R::Ok);
    CHECK_FALSE(session.active()); CHECK(backend.commits == 1); CHECK(backend.aborts == 0);
}

TEST_CASE("firmware upload session: an explicit abort failure is not reported as a successful abort") {
    struct ReportingBackend : UploadBackend {
        bool failAbort = false;
        bool abort() { ++aborts; return !failAbort; }
    } backend;
    using Session = mm::FirmwareUploadSession<ReportingBackend>; using R = Session::Result;
    Session session(backend); Session::Digest digest{}; Session::Status status{};
    REQUIRE(session.begin(1, 4, digest) == R::Ok);
    backend.failAbort = true;
    CHECK(session.abort(1) == R::Io);
    REQUIRE(session.status(1, status));
    CHECK(status.phase == Session::Phase::Failed); CHECK(status.result == R::Io);
    CHECK_FALSE(session.active()); CHECK(backend.aborts == 1);
    backend.failAbort = false;
    REQUIRE(session.begin(2, 4, digest) == R::Ok);
    CHECK(session.abort(2) == R::Ok);
    REQUIRE(session.status(2, status)); CHECK(status.phase == Session::Phase::Aborted);
}

TEST_CASE("firmware upload session: timeout preserves failed abort evidence for reconnect") {
    struct ReportingBackend : UploadBackend {
        bool abort() { ++aborts; return false; }
    } backend;
    using Session = mm::FirmwareUploadSession<ReportingBackend>; using R = Session::Result;
    Session session(backend); Session::Digest digest{}; Session::ResumeToken token{};
    token[0] = 7;
    Session::Status status{};
    REQUIRE(session.begin(1, 4, digest, token) == R::Ok);
    session.noteProgress(1, 100);
    CHECK(session.expireIdle(120, 20));
    REQUIRE(session.terminalStatus(token, status));
    CHECK(status.phase == Session::Phase::Failed); CHECK(status.result == R::Io);
    CHECK_FALSE(status.hasVerifiedDigest); CHECK_FALSE(session.active());
    CHECK(session.resume(2, token, 121) == R::Owner);
    REQUIRE(session.begin(3, 4, digest, token) == R::Ok);
    REQUIRE(session.disconnected(3, 200, 20) == R::Ok);
    CHECK(session.expire(220));
    REQUIRE(session.terminalStatus(token, status));
    CHECK(status.phase == Session::Phase::Failed); CHECK(status.result == R::Io);
    CHECK_FALSE(status.hasVerifiedDigest); CHECK_FALSE(session.active());
    CHECK(session.resume(4, token, 221) == R::Owner);
    CHECK(backend.aborts == 2);
}

TEST_CASE("firmware upload session: incorrect image, I/O failure and disconnect abandon the inactive slot") {
    UploadBackend backend; const uint8_t bytes[] = {1,2,3,4}; std::array<uint8_t, 32> wrong{};
    using Session = mm::FirmwareUploadSession<UploadBackend>; using R = Session::Result;
    {
        Session session(backend); REQUIRE(session.begin(1, 4, wrong) == R::Ok);
        REQUIRE(session.append(1, 0, bytes, 4) == R::Ok);
        CHECK(session.finish(1) == R::Integrity); CHECK(backend.commits == 0); CHECK(backend.aborts == 1);
        Session::Status result{}; REQUIRE(session.status(1, result));
        CHECK(result.phase == Session::Phase::Failed); CHECK(result.result == R::Integrity);
        REQUIRE(session.begin(2, 4, wrong) == R::Ok); backend.failWrite = true;
        CHECK_FALSE(session.status(1, result));
        CHECK(session.append(2, 0, bytes, 4) == R::Io); CHECK_FALSE(session.active()); CHECK(backend.aborts == 2);
        backend.failWrite = false; REQUIRE(session.begin(3, 4, wrong) == R::Ok);
        CHECK(session.abort(4) == R::Owner); CHECK(session.active());
    }
    CHECK(backend.aborts == 3); CHECK(backend.commits == 0);
}

TEST_CASE("firmware upload session: split image uses streaming digest before committing") {
    UploadBackend backend; backend.limit = 2048; mm::FirmwareUploadSession<UploadBackend> session(backend);
    using R = decltype(session)::Result;
    std::vector<uint8_t> image(1024); for (size_t i = 0; i < image.size(); ++i) image[i] = static_cast<uint8_t>(i);
    std::array<uint8_t, 32> digest{}; mm::sha256(image.data(), image.size(), digest.data());
    REQUIRE(session.begin(1, image.size(), digest) == R::Ok);
    for (size_t pos = 0; pos < image.size();) {
        const size_t left = image.size() - pos; const size_t n = left < 63 ? left : 63;
        REQUIRE(session.append(1, pos, image.data() + pos, n) == R::Ok); pos += n;
    }
    CHECK(session.finish(1) == R::Ok); CHECK(backend.data == image); CHECK(backend.commits == 1);
    REQUIRE(session.begin(2, image.size(), digest) == R::Ok); image[511] ^= 1;
    REQUIRE(session.append(2, 0, image.data(), image.size()) == R::Ok);
    CHECK(session.finish(2) == R::Integrity); CHECK(backend.commits == 1); CHECK(backend.aborts == 1);
}

TEST_CASE("firmware upload session: reconnect resumes only the correlated transfer before its deadline") {
    UploadBackend backend; using Session = mm::FirmwareUploadSession<UploadBackend>; using R = Session::Result;
    Session session(backend); const uint8_t image[] = {1,2,3,4}; Session::Digest digest{};
    mm::sha256(image, sizeof(image), digest.data()); Session::ResumeToken token{}, wrong{}; token[0] = 42; wrong[0] = 43;
    REQUIRE(session.begin(1, sizeof(image), digest, token) == R::Ok);
    REQUIRE(session.append(1, 0, image, 2) == R::Ok);
    CHECK(session.suspend(1, 0, 0) == R::Invalid);
    REQUIRE(session.suspend(1, UINT32_MAX - 10, 30) == R::Ok);
    CHECK(session.append(0, 2, image + 2, 2) == R::Owner);
    CHECK(session.resume(2, wrong, 5) == R::Owner); CHECK_FALSE(session.expire(5));
    REQUIRE(session.resume(2, token, 5) == R::Ok);
    CHECK(session.append(1, 2, image + 2, 2) == R::Owner);
    REQUIRE(session.append(2, 2, image + 2, 2) == R::Ok);
    REQUIRE(session.finish(2) == R::Ok); Session::Status status{};
    CHECK_FALSE(session.terminalStatus(wrong, status)); REQUIRE(session.terminalStatus(token, status));
    CHECK(status.phase == Session::Phase::Committed); CHECK(status.received == 4); CHECK(backend.commits == 1);
    CHECK(status.hasVerifiedDigest); CHECK(status.verifiedDigest == digest);
    REQUIRE(session.begin(3, sizeof(image), digest, token) == R::Ok); CHECK_FALSE(session.terminalStatus(token, status));
    REQUIRE(session.suspend(3, UINT32_MAX - 10, 30) == R::Ok);
    CHECK_FALSE(session.expire(18)); CHECK(session.expire(19)); CHECK_FALSE(session.active());
    REQUIRE(session.terminalStatus(token, status)); CHECK(status.phase == Session::Phase::Aborted);
    CHECK_FALSE(status.hasVerifiedDigest); CHECK(status.verifiedDigest == Session::Digest{});
    CHECK(backend.aborts == 1); CHECK_FALSE(session.expire(20));
}

TEST_CASE("firmware upload reports a verified digest only after successful commit") {
    UploadBackend backend; using Session = mm::FirmwareUploadSession<UploadBackend>; using R = Session::Result;
    Session session(backend); const uint8_t image[] = {1,2,3,4}; Session::Digest digest{};
    mm::sha256(image, sizeof(image), digest.data()); Session::Status status{};
    REQUIRE(session.begin(1, sizeof(image), digest) == R::Ok);
    REQUIRE(session.append(1, 0, image, sizeof(image)) == R::Ok);
    REQUIRE(session.status(1, status)); CHECK_FALSE(status.hasVerifiedDigest);
    backend.failCommit = true; REQUIRE(session.finish(1) == R::Io);
    REQUIRE(session.status(1, status)); CHECK(status.phase == Session::Phase::Failed);
    CHECK_FALSE(status.hasVerifiedDigest); CHECK(status.verifiedDigest == Session::Digest{});
    backend.failCommit = false; digest[0] ^= 1;
    REQUIRE(session.begin(2, sizeof(image), digest) == R::Ok);
    REQUIRE(session.append(2, 0, image, sizeof(image)) == R::Ok);
    REQUIRE(session.finish(2) == R::Integrity);
    REQUIRE(session.status(2, status)); CHECK_FALSE(status.hasVerifiedDigest);
    CHECK(status.verifiedDigest == Session::Digest{});
}

TEST_CASE("firmware upload backend: one gate prevents overlapping writers and is released on errors") {
    mm::FirmwareUpdateGate gate; UploadBackend firstPort, secondPort;
    mm::FirmwareUploadBackend first(firstPort, gate), second(secondPort, gate);
    mm::FirmwareUploadSession firstSession(first), secondSession(second);
    using R = decltype(firstSession)::Result;
    const uint8_t bytes[] = {1,2,3,4}; std::array<uint8_t, 32> digest{}; mm::sha256(bytes, sizeof(bytes), digest.data());
    firstPort.failBegin = true;
    CHECK(firstSession.begin(1, 4, digest) == R::Io); REQUIRE(gate.acquire()); gate.release();
    firstPort.failBegin = false; REQUIRE(firstSession.begin(1, 4, digest) == R::Ok);
    CHECK(secondSession.begin(2, 4, digest) == R::Io); CHECK_FALSE(gate.acquire());
    firstPort.failWrite = true; CHECK(firstSession.append(1, 0, bytes, 4) == R::Io);
    REQUIRE(gate.acquire()); gate.release(); CHECK(firstPort.aborts == 1);
    REQUIRE(secondSession.begin(2, 4, digest) == R::Ok);
    REQUIRE(secondSession.append(2, 0, bytes, 4) == R::Ok);
    secondPort.failCommit = true; CHECK(secondSession.finish(2) == R::Io);
    REQUIRE(gate.acquire()); gate.release(); CHECK(secondPort.commits == 1);
    REQUIRE(firstSession.begin(3, 4, digest) == R::Ok);
    CHECK(firstSession.abort(3) == R::Ok); REQUIRE(gate.acquire()); gate.release();
}

TEST_CASE("firmware update gate admits one of eight concurrent contenders") {
    mm::FirmwareUpdateGate gate; std::atomic<unsigned> ready{0}, finished{0}, admitted{0};
    CHECK_FALSE(gate.busy()); REQUIRE(gate.acquire()); CHECK(gate.busy()); gate.release(); CHECK_FALSE(gate.busy());
    std::atomic<bool> start{false}; std::array<std::thread, 8> threads;
    for (auto& thread : threads) thread = std::thread([&] {
        ready.fetch_add(1); while (!start.load()) std::this_thread::yield();
        const bool got = gate.acquire(); if (got) admitted.fetch_add(1);
        finished.fetch_add(1); while (finished.load() != threads.size()) std::this_thread::yield();
        if (got) gate.release();
    });
    while (ready.load() != threads.size()) std::this_thread::yield();
    start.store(true);
    for (auto& thread : threads) thread.join();
    CHECK(admitted.load() == 1); REQUIRE(gate.acquire()); gate.release();
}

TEST_CASE("firmware update lease transfers exclusive ownership and releases only once") {
    mm::FirmwareUpdateGate gate, otherGate;
    {
        mm::FirmwareUpdateLease producer(gate); REQUIRE(producer.acquire()); CHECK_FALSE(producer.acquire());
        mm::FirmwareUpdateLease worker(std::move(producer)); CHECK_FALSE(producer.held()); CHECK(worker.held());
        CHECK_FALSE(gate.acquire()); producer.release(); CHECK_FALSE(gate.acquire());
        mm::FirmwareUpdateLease replacement(otherGate); REQUIRE(replacement.acquire());
        replacement = std::move(worker); CHECK_FALSE(worker.held()); CHECK(replacement.held());
        REQUIRE(otherGate.acquire()); otherGate.release(); CHECK_FALSE(gate.acquire());
    }
    REQUIRE(gate.acquire()); gate.release();
}

TEST_CASE("firmware OTA port: app-slot writer is owned through commit, abort and failed write") {
    FakePlatformOta::begins = FakePlatformOta::commits = FakePlatformOta::aborts = 0;
    FakePlatformOta::failWrite = false;
    mm::FirmwareUpdateGate gate; mm::FirmwareOtaPort<FakePlatformOta> port;
    mm::FirmwareUploadBackend backend(port, gate); mm::FirmwareUploadSession session(backend);
    using R = decltype(session)::Result;
    const uint8_t image[] = {1,2,3,4}; std::array<uint8_t, 32> digest{}; mm::sha256(image, sizeof(image), digest.data());
    REQUIRE(session.begin(1, sizeof(image), digest) == R::Ok); CHECK(std::strcmp(port.status(), "ready") == 0);
    REQUIRE(session.append(1, 0, image, sizeof(image)) == R::Ok); CHECK(port.written() == sizeof(image));
    CHECK(session.finish(1) == R::Ok); CHECK(FakePlatformOta::commits == 1);
    REQUIRE(session.begin(2, sizeof(image), digest) == R::Ok);
    CHECK(session.abort(2) == R::Ok); CHECK(FakePlatformOta::aborts == 1);
    REQUIRE(session.begin(3, sizeof(image), digest) == R::Ok); FakePlatformOta::failWrite = true;
    CHECK(session.append(3, 0, image, sizeof(image)) == R::Io); CHECK_FALSE(session.active());
    REQUIRE(gate.acquire()); gate.release();
}

TEST_CASE("firmware OTA port: real desktop backend refuses safely and releases its gate") {
    struct Reset { ~Reset() { mm::platform::setTestOtaNextAppCapacity(0); } } reset;
    mm::platform::setTestOtaNextAppCapacity(4096);
    mm::FirmwareOtaPort<> port;
    static_assert(decltype(port)::kUsesPlatformUpdateGate);
    mm::FirmwareUploadSession session(port);
    std::array<uint8_t, 32> digest{};
    CHECK(session.begin(1, 1024, digest) == decltype(session)::Result::Io);
    CHECK_FALSE(session.active()); CHECK(std::strcmp(port.status(), "unsupported on desktop") == 0);
}

TEST_CASE("firmware OTA port: a platform-gated writer is used directly and excludes concurrent updates") {
    using Port = mm::FirmwareOtaPort<FakeAlreadyGatedOta>;
    Port firstPort, secondPort;
    mm::FirmwareUploadSession first(firstPort), second(secondPort);
    using R = decltype(first)::Result;
    const uint8_t image[] = {1, 2, 3, 4}; std::array<uint8_t, 32> digest{};
    mm::sha256(image, sizeof(image), digest.data());
    REQUIRE(first.begin(1, sizeof(image), digest) == R::Ok);
    CHECK(second.begin(2, sizeof(image), digest) == R::Io);
    CHECK_FALSE(FakeAlreadyGatedOta::gate.acquire());
    REQUIRE(first.append(1, 0, image, sizeof(image)) == R::Ok);
    REQUIRE(first.abort(1) == R::Ok);
    REQUIRE(second.begin(2, sizeof(image), digest) == R::Ok);
    REQUIRE(second.append(2, 0, image, sizeof(image)) == R::Ok);
    REQUIRE(second.finish(2) == R::Ok);
    REQUIRE(FakeAlreadyGatedOta::gate.acquire()); FakeAlreadyGatedOta::gate.release();
}

TEST_CASE("firmware upload bounds reject truncated and overlong known images") {
    CHECK(uploadFits(0, 0, 512, 1024, 1024));
    CHECK(uploadFits(512, 400, 112, 1024, 1024));
    CHECK_FALSE(uploadFits(512, 400, 113, 1024, 1024));
    CHECK_FALSE(uploadFits(512, 300, 2, 1024, 813));
    CHECK_FALSE(uploadFits(SIZE_MAX, 1, 1, SIZE_MAX, 0));
    CHECK(uploadFits(512, 400, 112, 1024, 0));
    CHECK(uploadComplete(512, 512, 1024));
    CHECK_FALSE(uploadComplete(512, 511, 1024));
    CHECK_FALSE(uploadComplete(512, 513, 1024));
    CHECK_FALSE(uploadComplete(SIZE_MAX, 1, 1024));
    CHECK(uploadComplete(100, 0, 0));
}

namespace {

std::filesystem::path repoRoot() {
    // test/unit/core/<this file> -> repo root
    return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
}

/// A synthetic image head: magic, chip id, and a descriptor naming a project.
std::vector<uint8_t> makeImage(uint8_t magic, uint16_t chip, const char* project,
                               const char* version = "4.0.0-dev", bool withDesc = true) {
    std::vector<uint8_t> b(kIdentifyBytes, 0);
    b[0] = magic;
    b[kChipIdOffset] = static_cast<uint8_t>(chip & 0xFF);
    b[kChipIdOffset + 1] = static_cast<uint8_t>(chip >> 8);
    if (withDesc) {
        const uint32_t m = kDescMagic;
        std::memcpy(b.data() + kDescOffset, &m, 4);
        std::memcpy(b.data() + kDescOffset + 16, version, std::strlen(version));
        std::memcpy(b.data() + kDescOffset + 48, project, std::strlen(project));
    }
    return b;
}

std::vector<uint8_t> readHead(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return {};
    std::vector<uint8_t> b(kIdentifyBytes);
    f.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size()));
    b.resize(static_cast<size_t>(f.gcount()));
    return b;
}

} // namespace

TEST_CASE("application image chip must match before upload writes") {
    const auto image = makeImage(kImageMagic, static_cast<uint16_t>(ChipId::Esp32S3), "MoonLight");
    const auto info = identify(image.data(), image.size());
    CHECK(appImageChipMatches(info, ChipId::Esp32S3));
    CHECK_FALSE(appImageChipMatches(info, ChipId::Esp32));
    CHECK_FALSE(appImageChipMatches(identify(nullptr, 0), ChipId::Esp32S3));
    const auto shortHeader = identify(image.data(), kChipIdOffset);
    CHECK_FALSE(appImageChipMatches(shortHeader, ChipId::Esp32S3));
}

namespace {

/// The built images present in the tree, as (path, expected project name) pairs: @xref{empty-means-not-built-not-passed}.
std::vector<std::pair<std::filesystem::path, std::string>> builtImages() {
    std::vector<std::pair<std::filesystem::path, std::string>> out;
    const auto build = repoRoot() / "build";
    if (!std::filesystem::exists(build)) return out;
    for (const auto& e : std::filesystem::directory_iterator(build)) {
        if (!e.is_directory()) continue;
        // The ESP32 builds' directories: a Teensy build writes a MoonLight.bin too, an image of another format.
        const std::string dir = e.path().filename().string();
        if (dir.rfind("esp32-", 0) != 0 && dir.rfind("moonbase-", 0) != 0) continue;
        for (const auto& [file, project] :
             {std::pair<const char*, const char*>{"MoonLight-moonbase.bin", "MoonLight-moonbase"},
              std::pair<const char*, const char*>{"MoonLight.bin", "MoonLight"}}) {
            const auto p = e.path() / file;
            if (std::filesystem::exists(p)) out.emplace_back(p, project);
        }
    }
    return out;
}

} // namespace

TEST_CASE("every built image identifies as what it is") {
    // THE PIN THAT MATTERS: our offsets against images the ESP32 toolchain actually produced: @xref{real-binaries-not-only-synthetic-ones}.
    const auto images = builtImages();
    if (images.empty()) {
        MESSAGE("no built ESP32 images in the tree; run build_esp32.py to exercise this");
        return;                     // a fresh checkout, not a failure
    }
    for (const auto& [path, project] : images) {
        CAPTURE(path.string());
        const auto head = readHead(path);
        REQUIRE(head.size() >= kIdentifyBytes);
        const auto info = identify(head.data(), head.size());
        CHECK(info.valid);                                  // begins with the image magic
        CHECK(info.described);                              // carries a readable descriptor
        CHECK(info.chip != ChipId::Invalid);                // names a chip we know
        CHECK(std::string(info.project) == project);
        // And the rule the install path runs: a MoonBase image is accepted for its own chip, an app image is refused whatever the chip. Both directions, against real binaries.
        const char* verdict = moonBaseRejection(info, info.chip);
        if (project == "MoonLight-moonbase") CHECK(verdict == nullptr);
        else CHECK(verdict == std::string("not a MoonBase image"));
    }
    MESSAGE("checked ", images.size(), " built image(s)");
}

TEST_CASE("built ESP32 and S3 applications reject each other's chip") {
    const auto classic = readHead(repoRoot() / "build/esp32-nebulite-esp32/MoonLight.bin");
    const auto s3 = readHead(repoRoot() / "build/esp32-helio1/MoonLight.bin");
    if (classic.empty() || s3.empty()) {
        MESSAGE("both ESP32 and S3 images are required for the real-image check");
        return;
    }
    REQUIRE(classic.size() >= kIdentifyBytes);
    REQUIRE(s3.size() >= kIdentifyBytes);
    const auto classicInfo = identify(classic.data(), classic.size());
    const auto s3Info = identify(s3.data(), s3.size());
    CHECK(appImageChipMatches(classicInfo, ChipId::Esp32));
    CHECK_FALSE(appImageChipMatches(classicInfo, ChipId::Esp32S3));
    CHECK(appImageChipMatches(s3Info, ChipId::Esp32S3));
    CHECK_FALSE(appImageChipMatches(s3Info, ChipId::Esp32));
}

TEST_CASE("optional firmware variant tags distinguish malformed and historical images") {
    std::vector<uint8_t> bytes(mm::firmware::kVariantOffset + mm::firmware::kVariantBytes, 0);
    using mm::firmware::VariantStatus;
    CHECK(mm::firmware::identifyVariant(bytes.data(), bytes.size()).status == VariantStatus::Missing);
    std::memcpy(bytes.data() + mm::firmware::kVariantOffset, mm::firmware::kVariantMagic, 8);
    CHECK(mm::firmware::identifyVariant(bytes.data(), bytes.size() - 1).status == VariantStatus::Malformed);
    CHECK(mm::firmware::identifyVariant(bytes.data(), bytes.size()).status == VariantStatus::Malformed);
    std::memcpy(bytes.data() + mm::firmware::kVariantOffset + 8, "helio1", 7);
    auto tag = mm::firmware::identifyVariant(bytes.data(), bytes.size());
    CHECK(tag.status == VariantStatus::Valid); CHECK(std::strcmp(tag.name, "helio1") == 0);
    bytes[mm::firmware::kVariantOffset + 8] = '/';
    CHECK(mm::firmware::identifyVariant(bytes.data(), bytes.size()).status == VariantStatus::Malformed);
    std::memset(bytes.data() + mm::firmware::kVariantOffset + 8, 'a', 32);
    CHECK(mm::firmware::identifyVariant(bytes.data(), bytes.size()).status == VariantStatus::Malformed);
}

TEST_CASE("an HTML error page is refused before anything is erased") {
    // What a wrong URL actually delivers: a 404 body or a login page, with a 200 status.
    const char* html = "<!DOCTYPE html><html><head><title>404 Not Found</title></head>";
    const auto info = identify(reinterpret_cast<const uint8_t*>(html), std::strlen(html));
    CHECK_FALSE(info.valid);
    CHECK(moonBaseRejection(info, ChipId::Esp32S3) == std::string("not a firmware image"));
}

TEST_CASE("an image for the wrong chip is refused") {
    // There is one MoonBase per chip and they are one paste apart. A checksum does NOT catch this: the image is perfectly valid, it just cannot execute on this silicon.
    const auto classic = makeImage(kImageMagic, 0x0000, "MoonLight-moonbase");
    const auto info = identify(classic.data(), classic.size());
    CHECK(info.valid);
    CHECK(info.chip == ChipId::Esp32);
    CHECK(moonBaseRejection(info, ChipId::Esp32) == nullptr);            // right chip: accepted
    CHECK(moonBaseRejection(info, ChipId::Esp32S3) == std::string("image is for another chip"));
}

TEST_CASE("an image with no description is refused") {
    const auto bare = makeImage(kImageMagic, 0x0009, "", "", /*withDesc=*/false);
    const auto info = identify(bare.data(), bare.size());
    CHECK(info.valid);
    CHECK_FALSE(info.described);
    CHECK(moonBaseRejection(info, ChipId::Esp32S3) == std::string("image carries no description"));
}

TEST_CASE("a truncated stream reports what it can rather than reading past its buffer") {
    // The first chunk can arrive short. Reading a fixed 128 bytes from a 40-byte buffer is how a vetting check becomes the vulnerability it was added to prevent.
    const auto full = makeImage(kImageMagic, 0x0009, "MoonLight-moonbase");
    for (size_t n : {size_t{0}, size_t{1}, size_t{12}, size_t{13}, size_t{31}, size_t{40},
                     kDescOffset + 95}) {
        const auto info = identify(full.data(), n);         // must not read past n
        if (n == 0) { CHECK_FALSE(info.valid); continue; }
        CHECK(info.valid);                                   // the magic is there
        CHECK_FALSE(info.described);                         // but the descriptor is not complete
        CHECK(moonBaseRejection(info, ChipId::Esp32S3) != nullptr);
    }
}

TEST_CASE("a 32-character project name that fills its field stays terminated") {
    // IDF leaves a name that exactly fills its 32 bytes unterminated. Copying all 32 into a 32-byte buffer would leave no NUL, and every later strcmp would run off the end.
    auto b = makeImage(kImageMagic, 0x0009, "MoonLight-moonbase");
    std::memset(b.data() + kDescOffset + 48, 'x', 32);       // no terminator in the field
    const auto info = identify(b.data(), b.size());
    CHECK(std::strlen(info.project) == 31);                  // truncated, and NUL-terminated
    CHECK(moonBaseRejection(info, ChipId::Esp32S3) == std::string("not a MoonBase image"));
}

TEST_CASE("an app image is accepted under the name it ships with and the name v6.0.0 shipped with") {
    // The new name ships, and the old one stays accepted so a device can still go back to the release that bridged them.
    CHECK(isAppImage("MoonLight"));
    CHECK(isAppImage("projectMM"));
    CHECK(std::string(kAppImageNames[0]) == "MoonLight");
}

TEST_CASE("a stranger's image is refused, however close its name") {
    CHECK_FALSE(isAppImage("WLED"));
    CHECK_FALSE(isAppImage(""));
    CHECK_FALSE(isAppImage("MoonLight2"));        // a longer name that merely begins the same
    CHECK_FALSE(isAppImage("MoonLigh"));          // and a shorter one
    CHECK_FALSE(isAppImage("moonlight"));         // the descriptor is case-sensitive
}

TEST_CASE("a recovery image is recognized under both names and never passes as an app") {
    // The guard that keeps a recovery image out of the app slot has to know the next name too: one it does not recognize is written over the app.
    for (const char* name : {"projectMM-moonbase", "MoonLight-moonbase"}) {
        CAPTURE(name);
        CHECK(isMoonBaseImage(name));
        CHECK_FALSE(isAppImage(name));
        const auto image = makeImage(kImageMagic, 0x0009, name);
        CHECK(moonBaseRejection(identify(image.data(), image.size()), ChipId::Esp32S3) == nullptr);
    }
    for (const char* name : {"projectMM", "MoonLight"}) {
        CAPTURE(name);
        CHECK_FALSE(isMoonBaseImage(name));
    }
}

TEST_CASE("a name that fills its descriptor field unterminated is still compared within the field") {
    // ESP-IDF's own descriptor field is 32 bytes and may carry no terminator, so the compare is bounded by the field.
    char field[32];
    std::memset(field, 'x', sizeof(field));
    CHECK_FALSE(isAppImage(field, sizeof(field)));
    std::memcpy(field, "MoonLight", 10);          // terminated inside the field, junk after it
    CHECK(isAppImage(field, sizeof(field)));
}
