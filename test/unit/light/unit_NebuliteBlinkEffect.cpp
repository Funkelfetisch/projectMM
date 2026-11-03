// @module NebuliteBlinkEffect
#include "doctest.h"
#include "light/effects/NebuliteBlinkEffect.h"
#include "light/layouts/GridLayout.h"
#include "light/layouts/Layouts.h"

TEST_CASE("Wearable blink renders the legacy segment and secondary RGBW") {
    mm::Layouts layouts;
    mm::GridLayout grid;
    grid.width = 12; grid.height = 1; grid.depth = 1;
    layouts.addChild(&grid);
    mm::Layer layer;
    layer.setLayouts(&layouts);
    layer.setChannelsPerLight(4);
    mm::NebuliteBlinkEffect effect;
    effect.segmentStart = 1; effect.segmentStop = 10;
    effect.secondaryWhite = 80;
    CHECK(effect.frameMs == 23); // archived 42 FPS, WLED integer division
    layer.addChild(&effect);
    layer.applyState();
    effect.renderAt(0);
    auto& frame = layer.buffer();
    REQUIRE(frame.count() == 12);
    CHECK(frame.data()[4 + 2] == 255);
    CHECK(frame.data()[4 + 3] == 0);
    CHECK(frame.data()[0] == 0);
    CHECK(frame.data()[10 * 4 + 2] == 0);
    effect.renderAt(275);
    CHECK(frame.data()[4 + 2] == 0);
    CHECK(frame.data()[4 + 3] == 80);
    CHECK(frame.data()[0 + 3] == 0);
    CHECK(frame.data()[10 * 4 + 3] == 0);
    effect.segmentStart = 10; effect.segmentStop = 3;
    effect.renderAt(548);
    for (size_t i = 0; i < frame.bytes(); ++i) CHECK(frame.data()[i] == 0);
}
