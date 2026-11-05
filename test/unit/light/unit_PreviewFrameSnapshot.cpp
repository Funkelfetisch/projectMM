// SPDX-License-Identifier: GPL-3.0-or-later
// The BLE preview frame: a pull snapshot that pages out one captured frame, pinned by its revision.
#include "doctest.h"
#include "core/module/MoonModule.h"
#include "light/drivers/PreviewFrameSnapshot.h"

#include <string>

using namespace mm;

namespace {
struct Owner : MoonModule {};

/// A two-page source frame: 64 RGB lights, light i lit (i, 0, 0).
struct Source {
    Buffer buffer;
    Source() { buffer.allocate(64, 3); for (uint16_t i = 0; i < 64; ++i) { buffer.data()[i * 3] = static_cast<uint8_t>(i); buffer.data()[i * 3 + 1] = buffer.data()[i * 3 + 2] = 0; } }
};

constexpr uint8_t kNoWhite[3] = {0, 0, 0};

/// One page: the error name, or the reply's text with an empty error.
struct Page { std::string error, text; };

Page page(PreviewFrameSnapshot& snapshot, uint16_t offset, const char* revision, uint32_t now) {
    char bytes[512]{};
    JsonSink sink(bytes, sizeof(bytes));
    const char* error = snapshot.write(sink, offset, revision, now);
    return {error ? error : "", std::string(bytes, sink.size())};
}

/// The 32-character revision a ready page names.
std::string revisionOf(const Page& p) {
    const auto at = p.text.find("\"revision\":\"");
    return at == std::string::npos ? std::string() : p.text.substr(at + 12, 32);
}

/// A snapshot holding a frame captured at `capturedAt`: the first request asks for it, the encode task takes it.
void capture(PreviewFrameSnapshot& snapshot, const Source& source, uint32_t capturedAt) {
    CHECK(page(snapshot, 0, nullptr, capturedAt).error == "frame-pending");
    snapshot.captureIfRequested(&source.buffer, kNoWhite, capturedAt);
}
} // namespace

TEST_CASE("the first preview request is pending, and the frame it asked for is captured for the next") {
    Owner owner; PreviewFrameSnapshot snapshot(owner); snapshot.reserve(64); Source source;
    capture(snapshot, source, 1000);
    const Page first = page(snapshot, 0, nullptr, 1010);
    CHECK(first.error.empty());
    CHECK(first.text.find("\"total\":64,\"offset\":0") != std::string::npos);
    CHECK(first.text.find("\"next\":32") != std::string::npos);
    CHECK(revisionOf(first).size() == 32);
}

TEST_CASE("a continuation page finishes its frame however long the earlier pages took") {
    Owner owner; PreviewFrameSnapshot snapshot(owner); snapshot.reserve(64); Source source;
    capture(snapshot, source, 1000);
    const Page first = page(snapshot, 0, nullptr, 1000);
    REQUIRE(first.error.empty());
    // Seven seconds later, on a slow link, the second page still describes the frame the first one began.
    const Page second = page(snapshot, 32, revisionOf(first).c_str(), 8000);
    CHECK(second.error.empty());
    CHECK(second.text.find("\"offset\":32,\"rgb\":\"200000") != std::string::npos);  // light 32 is (32, 0, 0)
    CHECK(second.text.find("\"next\":null") != std::string::npos);
}

TEST_CASE("a new request for a frame older than five seconds is pending, while a continuation of it is not") {
    Owner owner; PreviewFrameSnapshot snapshot(owner); snapshot.reserve(64); Source source;
    capture(snapshot, source, 1000);
    const std::string revision = revisionOf(page(snapshot, 0, nullptr, 1000));
    CHECK(page(snapshot, 0, nullptr, 1000 + PreviewFrameSnapshot::kMaxAgeMs).error.empty());
    CHECK(page(snapshot, 0, nullptr, 1001 + PreviewFrameSnapshot::kMaxAgeMs).error == "frame-pending");
    CHECK(page(snapshot, 32, revision.c_str(), 1001 + PreviewFrameSnapshot::kMaxAgeMs).error.empty());
}

TEST_CASE("a continuation naming another frame's revision reports the frame changed, however old") {
    Owner owner; PreviewFrameSnapshot snapshot(owner); snapshot.reserve(64); Source source;
    capture(snapshot, source, 1000);
    CHECK(page(snapshot, 32, "00000000000000000000000000000000", 1500).error == "frame-changed");
    CHECK(page(snapshot, 32, "00000000000000000000000000000000", 9000).error == "frame-changed");
    CHECK(page(snapshot, 32, nullptr, 9000).error == "frame-changed");
}

TEST_CASE("a frame captured just after the reader read its clock is fresh, not ages old") {
    Owner owner; PreviewFrameSnapshot snapshot(owner); snapshot.reserve(64); Source source;
    capture(snapshot, source, 2000);
    // The RPC tick read 1999, then the encode task captured at 2000 before the page was written.
    CHECK(page(snapshot, 0, nullptr, 1999).error.empty());
}

TEST_CASE("the frame age follows the millisecond clock across its wraparound") {
    Owner owner; PreviewFrameSnapshot snapshot(owner); snapshot.reserve(64); Source source;
    capture(snapshot, source, UINT32_MAX - 100);
    CHECK(page(snapshot, 0, nullptr, 1000).error.empty());   // 1101 ms later
    CHECK(page(snapshot, 0, nullptr, 6000).error == "frame-pending");   // 6101 ms later
}

TEST_CASE("a retired frame is pending for a new request and for a continuation alike") {
    Owner owner; PreviewFrameSnapshot snapshot(owner); snapshot.reserve(64); Source source;
    capture(snapshot, source, 1000);
    const std::string revision = revisionOf(page(snapshot, 0, nullptr, 1000));
    snapshot.invalidate();
    CHECK(page(snapshot, 32, revision.c_str(), 1100).error == "frame-pending");
    CHECK(page(snapshot, 0, nullptr, 1100).error == "frame-pending");
}
