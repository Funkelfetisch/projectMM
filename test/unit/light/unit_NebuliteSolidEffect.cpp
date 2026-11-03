// @module NebuliteSolidEffect
#include "doctest.h"
#include "light/effects/NebuliteSolidEffect.h"
#include "light/layouts/GridLayout.h"
#include "light/layouts/Layouts.h"

TEST_CASE("Wearable solid preserves blue's green component and physical segment") {
    mm::Layouts layouts;
    mm::GridLayout grid; grid.width = 91; grid.height = 1; grid.depth = 1;
    layouts.addChild(&grid);
    mm::Layer layer; layer.setLayouts(&layouts); layer.setChannelsPerLight(4);
    mm::NebuliteSolidEffect effect;
    effect.red = 0; effect.green = 8; effect.blue = 255; effect.white = 22;
    effect.segmentStart = 0; effect.segmentStop = 31;
    layer.addChild(&effect); layer.applyState(); layer.tick();
    const auto& frame = layer.buffer(); REQUIRE(frame.count() == 91);
    for (size_t i = 0; i < frame.count(); ++i) {
        const auto* pixel = frame.data() + i * 4;
        CHECK(pixel[0] == 0);
        CHECK(pixel[1] == (i < 31 ? 8 : 0));
        CHECK(pixel[2] == (i < 31 ? 255 : 0));
        CHECK(pixel[3] == (i < 31 ? 22 : 0));
    }
    effect.segmentStart = 40; effect.segmentStop = 31;
    layer.tick();
    for (size_t i = 0; i < frame.bytes(); ++i) CHECK(frame.data()[i] == 0);
}
