/// @module RingsEffect

#include "doctest.h"
#include "core/module/Scheduler.h"
#include "core/module/StateDocument.h"
#include "core/util/JsonSink.h"
#include "light/effects/RingsEffect.h"
#include "light/layouts/GridLayout.h"
#include "light/layouts/RingLayout.h"
#include "light/layers/BlendMap.h"
#include "light/layouts/Layouts.h"
#include "platform/platform.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {
struct RestoreRingClock {
    mm::Palette previousPalette = *mm::Palettes::active();
    RestoreRingClock() {
        mm::Palette visible;
        for (auto& color : visible.entry) color = {255, 255, 255};
        mm::Palettes::setActiveDirect(visible);
    }
    ~RestoreRingClock() {
        mm::platform::setTestNowMs(0);
        mm::Palettes::setActiveDirect(previousPalette);
    }
};

struct RingRig {
    RestoreRingClock clock;
    mm::Layouts layouts;
    mm::GridLayout grid;
    mm::Layer layer;
    mm::RingsEffect ring;

    explicit RingRig(uint32_t start = 100000) {
        grid.width = 32; grid.height = 32; grid.depth = 1;
        layouts.addChild(&grid);
        layer.setLayouts(&layouts);
        layer.setChannelsPerLight(3);
        ring.count = 1;
        ring.speed = 1;
        layer.addChild(&ring);
        layouts.applyState(); layer.applyState();
        at(start);
    }
    void at(uint32_t now) {
        mm::platform::setTestNowMs(now);
        layer.tick();
    }
    std::vector<uint8_t> pixels() {
        const auto& buffer = layer.buffer();
        REQUIRE(buffer.data() != nullptr);
        return {buffer.data(), buffer.data() + buffer.bytes()};
    }
};

std::vector<uint8_t> renderRing(uint32_t frames, uint32_t duration) {
    RingRig rig;
    for (uint32_t frame = 1; frame <= frames; ++frame)
        rig.at(100000 + static_cast<uint32_t>(static_cast<uint64_t>(frame) * duration / frames));
    return rig.pixels();
}
}

TEST_CASE("RingsEffect minimum speed renders outward intermediate motion between whole-pixel steps") {
    RingRig rig;
    const auto initial = rig.pixels();
    bool lit = false;
    for (uint8_t byte : initial) lit = lit || byte != 0;
    REQUIRE(lit);
    for (int frame = 0; frame < 2000; ++frame) rig.at(100000);
    CHECK(rig.pixels() == initial);
    rig.at(100016);
    const auto intermediate = rig.pixels();
    REQUIRE(intermediate != initial);
    bool brighter = false, dimmer = false;
    for (size_t pixel = 0; pixel < initial.size(); pixel += 3) {
        brighter = brighter || intermediate[pixel] > initial[pixel];
        dimmer = dimmer || intermediate[pixel] < initial[pixel];
    }
    CHECK(brighter);
    CHECK(dimmer); // the band moves outward, rather than only changing the whole frame's brightness
    for (int frame = 0; frame < 100; ++frame) rig.at(100016);
    CHECK(rig.pixels() == intermediate);
    auto previous = intermediate;
    for (uint32_t ms = 32; ms < 128; ms += 16) {
        rig.at(100000 + ms);
        const auto current = rig.pixels();
        CHECK(current != previous);
        previous = current;
    }
}

TEST_CASE("RingsEffect minimum speed reaches the same fractional rendered radius at different frame rates") {
    const auto oneStep = renderRing(1, 1079);
    CHECK(renderRing(8, 1079) == oneStep);
    CHECK(renderRing(64, 1079) == oneStep);
    CHECK(renderRing(1200, 1079) == oneStep);
    CHECK(renderRing(1, 1024) != oneStep);
}

TEST_CASE("RingsEffect begins at its seeded radius independently of controller uptime") {
    std::vector<uint8_t> early;
    {
        RingRig rig(1);
        early = rig.pixels();
    }
    RingRig late(100000);
    CHECK(late.pixels() == early);
}

TEST_CASE("RingsEffect carries fractional growth through a speed change without an absolute-time jump") {
    RingRig rig;
    const auto initial = rig.pixels();
    rig.at(100063);
    const auto beforeSpeedChange = rig.pixels();
    REQUIRE(beforeSpeedChange != initial);
    rig.ring.speed = 3;
    rig.at(100063);
    CHECK(rig.pixels() == beforeSpeedChange);
    rig.at(100080);
    CHECK(rig.pixels() == renderRing(1, 114)); // 63 ms at speed 1, then 17 ms at speed 3
}

TEST_CASE("RingsEffect minimum-speed growth retains its fraction across the millisecond wrap") {
    const auto after87 = renderRing(1, 87);
    RingRig rig(0xFFFFFFF0u);
    rig.at(0x10u);
    rig.at(0x47u);
    CHECK(rig.pixels() == after87);
}

TEST_CASE("RingsEffect background fills an empty ring phase uniformly and increases with its own brightness") {
    RingRig rig;
    CHECK(rig.ring.backgroundBrightness == 0);
    rig.ring.count = 0; // isolate the background from the additive ring bands
    rig.at(100000);
    for (uint8_t value : rig.pixels()) CHECK(value == 0);
    rig.ring.backgroundBrightness = 32;
    rig.at(100000);
    const auto low = rig.pixels();
    REQUIRE(low[0] > 0);
    for (uint8_t value : low) CHECK(value == low[0]);
    rig.ring.backgroundBrightness = 96;
    rig.at(100000);
    const auto high = rig.pixels();
    REQUIRE(high[0] > low[0]);
    for (uint8_t value : high) CHECK(value == high[0]);
}

TEST_CASE("RingsEffect background follows the selected palette hue and preserves a genuinely black sample") {
    RingRig rig;
    rig.ring.count = 0;
    rig.ring.backgroundBrightness = 255;
    mm::Palette palette;
    for (auto& color : palette.entry) color = {0, 0, 0};
    mm::Palettes::setActiveDirect(palette);
    rig.at(100000);
    for (uint8_t value : rig.pixels()) CHECK(value == 0);
    for (auto& color : palette.entry) color = {0, 0, 255};
    palette.entry[0] = {255, 0, 0};
    mm::Palettes::setActiveDirect(palette);
    rig.ring.hue_shift = 0;
    rig.at(100000);
    const auto red = rig.pixels();
    rig.ring.hue_shift = 128;
    rig.at(100000);
    const auto blue = rig.pixels();
    for (size_t pixel = 0; pixel < red.size(); pixel += 3) {
        CHECK(red[pixel] > 0); CHECK(red[pixel + 1] == 0); CHECK(red[pixel + 2] == 0);
        CHECK(blue[pixel] == 0); CHECK(blue[pixel + 1] == 0); CHECK(blue[pixel + 2] > 0);
    }
}

TEST_CASE("RingsEffect background brightness is a persistent byte control with strict bounds and native roundtrip") {
    mm::RingsEffect ring;
    ring.setName("Rings"); ring.setTypeName("RingsEffect");
    ring.defineControls();
    const mm::ControlDescriptor* background = nullptr;
    for (uint8_t i = 0; i < ring.controls().count(); ++i)
        if (!std::strcmp(ring.controls()[i].name, "background brightness")) background = &ring.controls()[i];
    REQUIRE(background != nullptr);
    CHECK(background->type == mm::ControlType::Uint8);
    CHECK(background->min == 0); CHECK(background->max == 255);
    CHECK_FALSE(background->hidden); CHECK_FALSE(background->readonly);
    CHECK(mm::isPersistable(*background));
    CHECK(ring.backgroundBrightness == 0);
    mm::Scheduler scheduler;
    scheduler.addModule(&ring);
    REQUIRE(scheduler.setControl("Rings", "background brightness", R"({"value":192})") == mm::Scheduler::SetControlResult::Ok);
    CHECK(ring.backgroundBrightness == 192);
    CHECK(scheduler.setControl("Rings", "background brightness", R"({"value":256})") == mm::Scheduler::SetControlResult::OutOfRange);
    CHECK(scheduler.setControl("Rings", "background brightness", R"({"value":-1})") == mm::Scheduler::SetControlResult::OutOfRange);
    CHECK(ring.backgroundBrightness == 192);
    mm::JsonSink saved;
    saved.append("{"); mm::writeStateMember(saved, ring); saved.append("}");
    REQUIRE_FALSE(saved.overflowed());
    ring.backgroundBrightness = 0;
    REQUIRE(mm::applyStateDocument(scheduler, saved.data(), mm::StateSource::Stored).ok);
    CHECK(ring.backgroundBrightness == 192);
}


namespace {
struct SparseRingRig {
    RestoreRingClock clock;
    mm::Layouts layouts;
    mm::RingLayout circle;
    mm::Layer layer;
    mm::RingsEffect ring;
    mm::Buffer physical;

    SparseRingRig() {
        circle.nrOfLEDs = 222; circle.radius = 24;
        layouts.addChild(&circle);
        layer.setLayouts(&layouts); layer.setChannelsPerLight(3);
        ring.count = 4; ring.speed = 1; ring.thickness = 5;
        layer.addChild(&ring);
        layouts.applyState(); layer.applyState();
        REQUIRE(physical.allocate(222, 3));
    }
    bool at(uint32_t now) {
        mm::platform::setTestNowMs(now); layer.tick();
        mm::blendMap(layer.buffer(), physical, layer.lut(), 3);
        for (size_t i = 0; i < physical.bytes(); ++i)
            if (physical.data()[i]) return true;
        return false;
    }
};

/// A physical emitter only in z=2 pins Layer's D2 extrusion, including sparse unmapped xy cells.
class ElevatedRingEmitter : public mm::LayoutBase {
public:
    mm::Dim dimensions() const override { return mm::Dim::D3; }
    mm::nrOfLightsType lightCount() const override { return 1; }
    void placeLights(const mm::CoordSink& sink) const override { sink.pixel(0, 100, 100, 2); }
};

struct ElevatedRingRig {
    RestoreRingClock clock;
    mm::Layouts layouts;
    ElevatedRingEmitter emitter;
    mm::Layer layer;
    mm::RingsEffect ring;
    mm::Buffer physical;
    explicit ElevatedRingRig(uint16_t pause) {
        layouts.addChild(&emitter);
        layer.setLayouts(&layouts); layer.setChannelsPerLight(3);
        ring.count = 1; ring.speed = 1; ring.thickness = 1; ring.maxPauseMs = pause;
        layer.addChild(&ring);
        layouts.applyState(); layer.applyState();
        REQUIRE(physical.allocate(1, 3));
    }
    bool at(uint32_t now) {
        mm::platform::setTestNowMs(now); layer.tick();
        mm::blendMap(layer.buffer(), physical, layer.lut(), 3);
        return physical.data()[0] || physical.data()[1] || physical.data()[2];
    }
};
}

TEST_CASE("RingsEffect bounds mapped 222-light HELIO dark pauses for minutes while retaining minimum speed") {
    SparseRingRig rig;
    REQUIRE(rig.layer.width() == 50);
    REQUIRE(rig.layer.height() == 51);
    CHECK(rig.ring.maxPauseMs == 500);
    const auto bytesBefore = mm::platform::allocatedBytes();
    uint32_t lastVisible = 100000;
    bool sawDark = false, sawVisible = false;
    for (uint32_t elapsed = 0; elapsed <= 180000; elapsed += 100) {
        const uint32_t now = 100000 + elapsed;
        if (rig.at(now)) { lastVisible = now; sawVisible = true; }
        else { sawDark = true; CHECK(now - lastVisible < 500); }
    }
    CHECK(sawVisible); CHECK(sawDark);
    CHECK(mm::platform::allocatedBytes() == bytesBefore);
    CHECK(rig.ring.speed == 1); CHECK(rig.ring.count == 4);
    CHECK(rig.ring.backgroundBrightness == 0);
}

TEST_CASE("RingsEffect zero pause lights a mapped depth-extruded emitter on the first dark frame") {
    ElevatedRingRig rig(0);
    REQUIRE(rig.layer.depth() == 3);
    CHECK(rig.at(100000));
    const auto first = rig.physical.data()[0];
    REQUIRE(first > 0);
    CHECK(rig.at(100000));
    CHECK(rig.physical.data()[0] == first); // repeated timestamp leaves a rescued ring stable
}

TEST_CASE("RingsEffect pause ceiling and live zero setting use the mapped source clock across wrap") {
    ElevatedRingRig rig(2000);
    const uint32_t start = 0xFFFFFF00u;
    REQUIRE_FALSE(rig.at(start)); // most raster pixels can glow while the one actual emitter stays dark
    CHECK_FALSE(rig.at(start + 1999u));
    CHECK(rig.at(start + 2000u));
    ElevatedRingRig live(2000);
    REQUIRE_FALSE(live.at(100000));
    live.ring.maxPauseMs = 0;
    CHECK(live.at(100000)); // zero applies live, rather than disabling the limiter
}

TEST_CASE("RingsEffect dark-pause rescue respects black palettes and empty ring populations") {
    ElevatedRingRig rig(0);
    mm::Palette black;
    for (auto& color : black.entry) color = {0, 0, 0};
    mm::Palettes::setActiveDirect(black);
    for (uint32_t elapsed = 0; elapsed < 3000; elapsed += 100) CHECK_FALSE(rig.at(100000 + elapsed));
    black.entry[7] = {0, 0, 255}; // rescue may choose a visible color from this same palette
    mm::Palettes::setActiveDirect(black);
    CHECK(rig.at(103000));
    CHECK(rig.physical.data()[0] == 0); CHECK(rig.physical.data()[1] == 0);
    CHECK(rig.physical.data()[2] > 0);
    rig.ring.count = 0;
    CHECK_FALSE(rig.at(103001));
}

TEST_CASE("RingsEffect max pause is a persistent Uint16 control defaulting to 500 with strict 0 to 2000 bounds") {
    mm::RingsEffect ring;
    ring.setName("Rings"); ring.setTypeName("RingsEffect"); ring.defineControls();
    const mm::ControlDescriptor* pause = nullptr;
    for (uint8_t i = 0; i < ring.controls().count(); ++i)
        if (!std::strcmp(ring.controls()[i].name, "max pause ms")) pause = &ring.controls()[i];
    REQUIRE(pause != nullptr);
    CHECK(pause->type == mm::ControlType::Uint16);
    CHECK(pause->min == 0); CHECK(pause->max == 2000);
    CHECK_FALSE(pause->hidden); CHECK_FALSE(pause->readonly); CHECK(mm::isPersistable(*pause));
    CHECK(ring.maxPauseMs == 500);
    mm::Scheduler scheduler; scheduler.addModule(&ring);
    REQUIRE(scheduler.setControl("Rings", "max pause ms", R"({"value":0})") == mm::Scheduler::SetControlResult::Ok);
    CHECK(ring.maxPauseMs == 0);
    REQUIRE(scheduler.setControl("Rings", "max pause ms", R"({"value":2000})") == mm::Scheduler::SetControlResult::Ok);
    CHECK(scheduler.setControl("Rings", "max pause ms", R"({"value":2001})") == mm::Scheduler::SetControlResult::OutOfRange);
    CHECK(scheduler.setControl("Rings", "max pause ms", R"({"value":-1})") == mm::Scheduler::SetControlResult::OutOfRange);
    mm::JsonSink saved; saved.append("{"); mm::writeStateMember(saved, ring); saved.append("}");
    REQUIRE_FALSE(saved.overflowed()); ring.maxPauseMs = 500;
    REQUIRE(mm::applyStateDocument(scheduler, saved.data(), mm::StateSource::Stored).ok);
    CHECK(ring.maxPauseMs == 2000);
}
