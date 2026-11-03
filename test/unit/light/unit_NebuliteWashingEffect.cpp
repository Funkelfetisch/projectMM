// @module NebuliteWashingEffect
#include "doctest.h"
#include "light/effects/NebuliteWashingEffect.h"
#include "light/effects/NebuliteSineEffect.h"
#include "light/effects/NebulitePaletteEffect.h"
#include "light/layouts/GridLayout.h"
#include "light/layouts/Layouts.h"
#include <vector>

TEST_CASE("Wearable palette keeps physical segment, RGB colors and frame timing") {
    mm::Layouts layouts;
    mm::GridLayout grid; grid.width = 12; grid.height = 1; grid.depth = 1; layouts.addChild(&grid);
    mm::Layer layer; layer.setLayouts(&layouts); layer.setChannelsPerLight(4);
    mm::NebulitePaletteEffect effect; effect.segmentStart = 2; effect.segmentStop = 9;
    layer.addChild(&effect); layer.applyState(); layer.tick(); effect.renderAt(1000);
    auto& frame = layer.buffer(); REQUIRE(frame.count() == 12); REQUIRE(frame.data());
    std::vector<uint8_t> saved(frame.data(), frame.data()+frame.bytes()); bool any = false;
    for (size_t i=0;i<frame.count();++i) {
        const auto* p=frame.data()+4*i;
        if (i>=2 && i<9) any |= (p[0]|p[1]|p[2]) != 0;
        else CHECK((p[0]|p[1]|p[2]) == 0);
        CHECK(p[3] == 0);
    }
    CHECK(any); effect.renderAt(1001);
    CHECK(std::equal(saved.begin(),saved.end(),frame.data()));
    effect.renderAt(1023); CHECK_FALSE(std::equal(saved.begin(),saved.end(),frame.data()));
    effect.segmentStart=10; effect.segmentStop=2; effect.renderAt(1046);
    for(size_t i=0;i<frame.bytes();++i) CHECK(frame.data()[i]==0);
}

TEST_CASE("Wearable sine respects segment boundaries, background and frame timing") {
    mm::Layouts layouts;
    mm::GridLayout grid; grid.width = 12; grid.height = 1; grid.depth = 1;
    layouts.addChild(&grid);
    mm::Layer layer; layer.setLayouts(&layouts); layer.setChannelsPerLight(4);
    mm::NebuliteSineEffect effect;
    effect.segmentStart = 3; effect.segmentStop = 9;
    effect.speed = 0; effect.intensity = 0; effect.secondaryRed = 42; effect.secondaryWhite = 73;
    layer.addChild(&effect); layer.applyState(); layer.tick();
    effect.renderAt(1000);
    auto& frame = layer.buffer(); REQUIRE(frame.count() == 12); REQUIRE(frame.data());
    for (size_t i = 0; i < frame.count(); ++i) {
        const auto* p = frame.data() + 4 * i;
        CHECK(p[0] == (i >= 3 && i < 9 ? 42 : 0));
        CHECK(p[1] == 0); CHECK(p[2] == 0);
        CHECK(p[3] == (i >= 3 && i < 9 ? 73 : 0));
    }
    effect.speed = 128;
    effect.renderAt(1023);
    std::vector<uint8_t> saved(frame.data(), frame.data() + frame.bytes());
    effect.renderAt(1030);
    CHECK(std::equal(saved.begin(), saved.end(), frame.data()));
    effect.segmentStart = 10; effect.segmentStop = 3;
    effect.renderAt(1046);
    for (size_t i = 0; i < frame.bytes(); ++i) CHECK(frame.data()[i] == 0);
}

TEST_CASE("Wearable washing effect renders a bounded physical segment") {
    mm::Layouts layouts;
    mm::GridLayout grid; grid.width = 91; grid.height = 1; grid.depth = 1;
    layouts.addChild(&grid);
    mm::Layer layer; layer.setLayouts(&layouts); layer.setChannelsPerLight(4);
    mm::NebuliteWashingEffect effect;
    effect.segmentStart = 3; effect.segmentStop = 31;
    layer.addChild(&effect); layer.applyState(); layer.tick();
    effect.renderAt(1000);
    auto& frame = layer.buffer(); REQUIRE(frame.count() == 91); REQUIRE(frame.data());
    std::vector<uint8_t> saved(frame.data(), frame.data() + frame.bytes());
    bool any = false;
    for (size_t i = 0; i < frame.count(); ++i) {
        const auto* pixel = frame.data() + 4 * i;
        if (i >= 3 && i < 31) any |= (pixel[0] | pixel[1] | pixel[2]) != 0;
        else CHECK((pixel[0] | pixel[1] | pixel[2]) == 0);
        CHECK(pixel[3] == 0);
    }
    CHECK(any);
    effect.renderAt(1000);
    CHECK(std::equal(saved.begin(), saved.end(), frame.data()));
    effect.segmentStart = 40; effect.segmentStop = 31;
    effect.renderAt(1023);
    for (size_t i = 0; i < frame.bytes(); ++i) CHECK(frame.data()[i] == 0);
}
