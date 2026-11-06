/// @module PreviewDriver
/// @also RmtLedDriver, PwmLightDriver

/// The BLE preview frame: a pull snapshot that pages out one captured frame, pinned by its revision.
#include "doctest.h"
#include "core/module/MoonModule.h"
#include "light/drivers/FixtureProfilesModule.h"
#include "light/drivers/PreviewDriver.h"
#include "light/drivers/PreviewFrameSnapshot.h"
#include "light/drivers/PwmLightDriver.h"
#include "light/drivers/RmtLedDriver.h"

#include <string>
#include <vector>

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
    snapshot.captureIfRequested(&source.buffer, kNoWhite, PreviewOutputMap{}, capturedAt);
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

TEST_CASE("a garment's preview frame shows what each output emits: the RGB strips their colour, the white accent its white") {
    FixtureProfilesModule fixtures; fixtures.setup();
    MoonModule drivers; drivers.setName("Drivers");
    auto* preview = new PreviewDriver(); preview->setTypeName("PreviewDriver"); drivers.addChild(preview);
    // A candle scene on the four-channel layer: orange with white on every light.
    Buffer source; source.allocate(9, 4);
    for (uint16_t i = 0; i < 9; ++i) { uint8_t* px = source.data() + i * 4; px[0] = 255; px[1] = 120; px[2] = 0; px[3] = 200; }
    // The Croptop S3's shape: a strip of 8 mirrored on four RGB outputs, each window running to the end of the buffer as the profile sets it, and a white accent as light 8.
    std::vector<RmtLedDriver*> strips;
    const char* pins[] = {"36", "35", "48", "34"};
    for (const char* pin : pins) {
        auto* strip = new RmtLedDriver(); strip->setTypeName("RmtLedDriver"); strip->setName(pin);
        strip->setDefaultCorrection("RGB", WhiteMode::None); strip->rebuildControls(); strip->rebuildCorrection(255);
        std::snprintf(strip->pins, sizeof(strip->pins), "%s", pin);
        std::snprintf(strip->ledsPerPin, sizeof(strip->ledsPerPin), "%s", "8");
        strip->setWindow(0, 0); strip->setSourceBuffer(&source); strip->prepare();
        drivers.addChild(strip); strips.push_back(strip);
        REQUIRE(strip->lightsDriven() == 8);
    }
    auto* accent = new PwmLightDriver(); accent->setTypeName("PwmLightDriver"); accent->setName("Accent");
    accent->setDefaultCorrection("White", WhiteMode::None); accent->rebuildControls(); accent->rebuildCorrection(255);
    accent->setWindow(8, 1); drivers.addChild(accent);
    REQUIRE(accent->correction().offWhite != Correction::kAbsent);
    preview->setSourceBuffer(&source);
    preview->bleFrame().reserve(9);
    CHECK(page(preview->bleFrame(), 0, nullptr, 100).error == "frame-pending");
    preview->tick();
    const Page frame = page(preview->bleFrame(), 0, nullptr, 100);
    REQUIRE(frame.error.empty());
    // Lights 0-7 as the strips emit them, orange without white; light 8 as the accent emits it, white without colour.
    std::string expected;
    for (int i = 0; i < 8; ++i) expected += "ff7800";
    expected += "c8c8c8";
    CHECK(frame.text.find("\"rgb\":\"" + expected + "\"") != std::string::npos);

    // With the strips off, lights 0-7 have no output, and the preview shows them as composed, white included.
    for (auto* strip : strips) strip->setEnabled(false);
    CHECK(page(preview->bleFrame(), 0, nullptr, 200).error.empty());   // the last page asks for the next frame
    preview->tick();
    CHECK(page(preview->bleFrame(), 0, nullptr, 200).text.find("\"rgb\":\"ffffc8") != std::string::npos);
    preview->setSourceBuffer(nullptr);
    for (auto* strip : strips) strip->setSourceBuffer(nullptr);
}
