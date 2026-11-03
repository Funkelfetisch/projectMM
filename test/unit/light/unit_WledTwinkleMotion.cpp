// @module WledTwinkleMotion
#include "doctest.h"
#include "light/effects/WledColorTwinkles.h"
#include "light/effects/WledTwinkleMotion.h"
#include "light/effects/NebuliteFireflyEffect.h"
#include "light/effects/NebuliteSnowflakesEffect.h"
#include "light/effects/NebuliteFlowStripeEffect.h"
#include "light/effects/NebulitePlasmoidEffect.h"
#include "light/effects/NebuliteNoisefireEffect.h"
#include "light/layouts/GridLayout.h"
#include "light/layouts/Layouts.h"
#include <vector>

TEST_CASE("Noisefire renders its physical segment and bounds audio") {
    mm::Layouts layouts; mm::GridLayout grid;
    grid.width = 12; grid.height = 1; grid.depth = 1; layouts.addChild(&grid);
    mm::Layer layer; layer.setLayouts(&layouts); layer.setChannelsPerLight(4);
    mm::NebuliteNoisefireEffect effect; effect.segmentStart = 2; effect.segmentStop = 10;
    layer.addChild(&effect); layer.applyState(); effect.renderAt(0, 0);
    auto& frame = layer.buffer(); REQUIRE(frame.count() == 12); REQUIRE(frame.data());
    for (size_t i = 0; i < frame.bytes(); ++i) CHECK(frame.data()[i] == 0);
    effect.renderAt(12345, 65535);
    std::vector<uint8_t> saved(frame.data(), frame.data() + frame.bytes());
    bool lit = false;
    for (size_t i = 0; i < frame.count(); ++i) {
        const auto* p = frame.data() + i * 4;
        if (i >= 2 && i < 10) {
            const auto c = mm::WledNoisefireFrame::pixel(i - 2, 8, 128, 128, 12345, 255);
            CHECK(p[0] == c.r); CHECK(p[1] == c.g); CHECK(p[2] == c.b);
            lit |= (p[0] | p[1] | p[2]) != 0;
        } else CHECK((p[0] | p[1] | p[2]) == 0);
        CHECK(p[3] == 0);
    }
    CHECK(lit);
    effect.renderAt(12346, 0); CHECK(std::equal(saved.begin(), saved.end(), frame.data()));
    effect.renderAt(12368, 0);
    for (size_t i = 0; i < frame.bytes(); ++i) CHECK(frame.data()[i] == 0);
}

TEST_CASE("wearable audio simulation is opt-in and never replaces a quiet active source") {
    mm::Layouts layouts; mm::GridLayout grid;
    grid.width = 12; grid.height = 1; grid.depth = 1; layouts.addChild(&grid);
    mm::Layer layer; layer.setLayouts(&layouts); layer.setChannelsPerLight(4);
    mm::NebuliteNoisefireEffect effect; layer.addChild(&effect); layer.applyState();
    auto& frame = layer.buffer(); REQUIRE(frame.data());
    effect.renderAt(23, 0, false);
    for (size_t i = 0; i < frame.bytes(); ++i) CHECK(frame.data()[i] == 0);
    effect.simulateIfNoSource = true; effect.renderAt(46, 0, false);
    bool lit = false; for (size_t i = 0; i < frame.bytes(); ++i) lit |= frame.data()[i] != 0;
    CHECK(lit);
    effect.renderAt(69, 0, true);
    for (size_t i = 0; i < frame.bytes(); ++i) CHECK(frame.data()[i] == 0);
}

TEST_CASE("wearable Noisefire tick detects a genuinely absent audio service") {
    REQUIRE(mm::AudioService::latestFrame() == &mm::AudioService::kSilence);
    mm::Layouts layouts; mm::GridLayout grid;
    grid.width = 12; grid.height = 1; grid.depth = 1; layouts.addChild(&grid);
    mm::Layer layer; layer.setLayouts(&layouts); layer.setChannelsPerLight(4);
    mm::NebuliteNoisefireEffect effect; effect.simulateIfNoSource = true;
    layer.addChild(&effect); layer.applyState(); effect.tick();
    auto& frame = layer.buffer(); REQUIRE(frame.data());
    bool lit = false; for (size_t i = 0; i < frame.bytes(); ++i) lit |= frame.data()[i] != 0;
    CHECK(lit);
}

TEST_CASE("Plasmoid renders the physical segment from audio and holds frames") {
    mm::Layouts layouts; mm::GridLayout grid;
    grid.width = 12; grid.height = 1; grid.depth = 1; layouts.addChild(&grid);
    mm::Layer layer; layer.setLayouts(&layouts); layer.setChannelsPerLight(4);
    mm::NebulitePlasmoidEffect effect; effect.segmentStart = 2; effect.segmentStop = 10;
    layer.addChild(&effect); layer.applyState(); effect.renderAt(0, 0);
    auto& frame = layer.buffer(); REQUIRE(frame.count() == 12); REQUIRE(frame.data());
    for (size_t i = 0; i < frame.bytes(); ++i) CHECK(frame.data()[i] == 0);
    effect.renderAt(23, 255);
    std::vector<uint8_t> saved(frame.data(), frame.data() + frame.bytes());
    bool lit = false;
    for (size_t i = 0; i < frame.count(); ++i) {
        const auto* p = frame.data() + i * 4;
        if (i >= 2 && i < 10) lit |= (p[0] | p[1] | p[2]) != 0;
        else CHECK((p[0] | p[1] | p[2] | p[3]) == 0);
    }
    CHECK(lit);
    effect.renderAt(24, 0); CHECK(std::equal(saved.begin(), saved.end(), frame.data()));
    effect.segmentStart = 10; effect.segmentStop = 10; effect.renderAt(46, 255);
    for (size_t i = 0; i < frame.bytes(); ++i) CHECK(frame.data()[i] == 0);
}

TEST_CASE("Flow Stripe renders its bounded physical segment and preserves frame cadence") {
    mm::Layouts layouts; mm::GridLayout grid;
    grid.width = 12; grid.height = 1; grid.depth = 1; layouts.addChild(&grid);
    mm::Layer layer; layer.setLayouts(&layouts); layer.setChannelsPerLight(4);
    mm::NebuliteFlowStripeEffect effect; effect.segmentStart = 2; effect.segmentStop = 10;
    layer.addChild(&effect); layer.applyState(); effect.renderAt(1000);
    auto& frame = layer.buffer(); REQUIRE(frame.count() == 12); REQUIRE(frame.data());
    std::vector<uint8_t> saved(frame.data(), frame.data() + frame.bytes());
    for (size_t i = 0; i < frame.count(); ++i) {
        const auto* p = frame.data() + i * 4;
        if (i < 2 || i >= 10) CHECK((p[0] | p[1] | p[2]) == 0);
        else CHECK((p[0] | p[1] | p[2]) != 0);
        CHECK(p[3] == 0);
    }
    effect.renderAt(1001); CHECK(std::equal(saved.begin(), saved.end(), frame.data()));
    effect.segmentStart = 2; effect.segmentStop = 3; effect.renderAt(1023);
    for (size_t i = 0; i < frame.bytes(); ++i) CHECK(frame.data()[i] == 0);
}

TEST_CASE("WLED ColorTwinkles spawns, brightens and preserves segment boundaries") {
    struct FixedRandom {
        unsigned calls = 0;
        uint8_t next8() { return (++calls % 2) ? 0 : 255; }
        uint16_t bounded16(uint16_t) { return 0; }
    } random;
    mm::RGB pixels[5] = {{7,8,9}, {}, {}, {}, {4,5,6}};
    uint8_t rising[3] = {};
    mm::WledColorTwinkles::step(pixels + 1, rising, 3, 128, 255, 255, random);
    CHECK(pixels[0].r == 7); CHECK(pixels[4].r == 4);
    CHECK(pixels[1].r == 65); CHECK(pixels[1].g == 65); CHECK(pixels[1].b == 65);
    CHECK(rising[0] == 1); CHECK(pixels[2].r == 0);
    mm::WledColorTwinkles::step(pixels + 1, rising, 3, 128, 255, 255, random);
    CHECK(pixels[1].r == 76); CHECK(pixels[1].g == 76); CHECK(pixels[1].b == 76);
    CHECK(pixels[0].r == 7); CHECK(pixels[4].r == 4);
}

TEST_CASE("WLED Twinkleup keeps the archived seed, sine and palette-index sequence") {
    // Golden values from the unchanged archived mode_twinkleup and WLED sin8_t.
    mm::WledTwinkleMotion motion(0, 128, 255);
    const unsigned brightness[] = {114, 141, 234, 34, 5, 184};
    const unsigned index[] = {84, 0, 163, 130, 73, 139};
    for (unsigned i = 0; i < 6; ++i) {
        const auto pixel = motion.next();
        CHECK(pixel.brightness == brightness[i]);
        CHECK(pixel.paletteIndex == index[i]);
    }
}

TEST_CASE("WLED Twinkleup intensity filters pixels without moving their palette sequence") {
    mm::WledTwinkleMotion all(1000, 255, 255), few(1000, 255, 0);
    unsigned dark = 0;
    for (unsigned i = 0; i < 64; ++i) {
        const auto a = all.next(), b = few.next();
        CHECK(a.paletteIndex == b.paletteIndex);
        CHECK((b.brightness == 0 || b.brightness == a.brightness));
        if (b.brightness == 0) ++dark;
    }
    CHECK(dark > 50);
}

TEST_CASE("Firefly palette matches the archived Fire gradient at reference indices") {
    const auto a = mm::WledFirePalette::sample(0), b = mm::WledFirePalette::sample(128), c = mm::WledFirePalette::sample(255);
    CHECK((a.r == 0 && a.g == 0 && a.b == 0));
    CHECK((b.r == 127 && b.g == 1 && b.b == 0));
    CHECK((c.r == 255 && c.g == 255 && c.b == 71));
}

TEST_CASE("Firefly renders only its physical segment and keeps the frame deadline") {
    mm::Layouts layouts; mm::GridLayout grid;
    grid.width = 12; grid.height = 1; grid.depth = 1; layouts.addChild(&grid);
    mm::Layer layer; layer.setLayouts(&layouts); layer.setChannelsPerLight(4);
    mm::NebuliteFireflyEffect effect; effect.segmentStart = 2; effect.segmentStop = 10; effect.intensity = 255;
    layer.addChild(&effect); layer.applyState(); layer.tick(); effect.renderAt(1000);
    auto& frame = layer.buffer(); REQUIRE(frame.count() == 12); REQUIRE(frame.data());
    std::vector<uint8_t> saved(frame.data(), frame.data() + frame.bytes());
    bool lit = false;
    for (size_t i = 0; i < frame.count(); ++i) {
        const auto* p = frame.data() + i * 4;
        if (i >= 2 && i < 10) lit |= (p[0] | p[1] | p[2]) != 0;
        else CHECK((p[0] | p[1] | p[2] | p[3]) == 0);
        CHECK(p[3] == 0);
    }
    CHECK(lit); effect.renderAt(1001);
    CHECK(std::equal(saved.begin(), saved.end(), frame.data()));
    effect.renderAt(1023); CHECK_FALSE(std::equal(saved.begin(), saved.end(), frame.data()));
    effect.segmentStart = 10; effect.segmentStop = 2; effect.renderAt(1046);
    for (size_t i = 0; i < frame.bytes(); ++i) CHECK(frame.data()[i] == 0);
}

TEST_CASE("Snowflakes effect respects its segment, frame interval and release") {
    mm::Layouts layouts; mm::GridLayout grid;
    grid.width = 12; grid.height = 1; grid.depth = 1; layouts.addChild(&grid);
    mm::Layer layer; layer.setLayouts(&layouts); layer.setChannelsPerLight(4);
    mm::NebuliteSnowflakesEffect effect; effect.segmentStart = 2; effect.segmentStop = 10; effect.intensity = 255;
    const auto estimate = effect.workingMemory({12, 1, 1}, 4);
    CHECK(estimate.bytes == 12 * (sizeof(mm::RGB) + 1)); CHECK(estimate.largestBlock == 12 * sizeof(mm::RGB));
    layer.addChild(&effect); layer.applyState();
    REQUIRE(effect.dynamicBytes() > 0);
    auto& frame = layer.buffer(); REQUIRE(frame.count() == 12); REQUIRE(frame.data());
    bool lit = false;
    for (uint32_t now = 1000; now < 1200; now += 24) {
        effect.renderAt(now);
        for (size_t i = 0; i < frame.count(); ++i) {
            const auto* p = frame.data() + i * 4;
            if (i >= 2 && i < 10) lit |= (p[0] | p[1] | p[2]) != 0;
            else CHECK((p[0] | p[1] | p[2] | p[3]) == 0);
            CHECK(p[3] == 0);
        }
    }
    CHECK(lit);
    std::vector<uint8_t> saved(frame.data(), frame.data() + frame.bytes());
    CHECK_FALSE(effect.affectsPrepare("speed")); CHECK_FALSE(effect.affectsPrepare("effect intensity"));
    effect.speed = 70; effect.intensity = 50;
    effect.renderAt(1193); CHECK(std::equal(saved.begin(), saved.end(), frame.data()));
    effect.prepare(); effect.renderAt(1194);
    CHECK(std::equal(saved.begin(), saved.end(), frame.data()));
    effect.segmentStart = 10; effect.segmentStop = 2; effect.renderAt(1220);
    for (size_t i = 0; i < frame.bytes(); ++i) CHECK(frame.data()[i] == 0);
    effect.release(); CHECK(effect.dynamicBytes() == 0);
}

TEST_CASE("Snowflakes follows live master brightness or uses its explicit reference") {
    struct Output : mm::LightOutput {
        virtual ~Output() = default;
        mm::LightSummary value{}; uint8_t master = 5;
        const mm::LightSummary& summary() const override { return value; }
        uint8_t masterBrightness() const override { return master; }
        uint8_t nearestPalette(uint16_t, uint8_t) const override { return 0; }
        void paletteHueSat(uint8_t, uint16_t&, uint16_t&) const override {}
        uint8_t paletteCount() const override { return 0; }
        void writePaletteNames(mm::JsonSink&) const override {}
    } output;
    mm::ActiveInstance<mm::LightOutput> seat(output); seat.claim();
    mm::Layouts layouts; mm::GridLayout grid; grid.width = 31; grid.height = grid.depth = 1; layouts.addChild(&grid);
    mm::Layer a, b; a.setLayouts(&layouts); b.setLayouts(&layouts);
    a.setChannelsPerLight(3); b.setChannelsPerLight(3);
    mm::NebuliteSnowflakesEffect automatic, manual; manual.followMaster = false;
    a.addChild(&automatic); b.addChild(&manual); a.applyState(); b.applyState();
    for (uint32_t now=1000; now<1600; now+=24) { automatic.renderAt(now); manual.renderAt(now); }
    REQUIRE(a.buffer().data()); REQUIRE(b.buffer().data());
    CHECK_FALSE(std::equal(a.buffer().data(), a.buffer().data() + a.buffer().bytes(), b.buffer().data()));
    seat.vacate();
}
