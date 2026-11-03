// @module HelioFactoryTestEffect

#include "doctest.h"
#include "light/effects/HelioFactoryTestEffect.h"
#include "light/layouts/Layouts.h"
#include "light/layouts/RingLayout.h"
#include "light/layers/Layer.h"
#include "platform/platform.h"

#include <array>
#include <cstdint>
#include <utility>

namespace {

struct RingFixture {
    mm::Layouts layouts;
    mm::RingLayout ring;
    mm::Layer layer;
    mm::HelioFactoryTestEffect factory;

    explicit RingFixture(uint16_t count, uint8_t channels = 4) {
        ring.nrOfLEDs = count;
        layouts.addChild(&ring);
        layer.setLayouts(&layouts);
        layer.setChannelsPerLight(channels);
        layer.addChild(&factory);
        layer.applyState();
        REQUIRE(layer.buffer().count() == count);
        REQUIRE(layer.buffer().bytes() == static_cast<size_t>(count) * channels);
    }

    ~RingFixture() { mm::platform::setTestNowMs(0); }

    void frame(uint32_t when) {
        mm::platform::setTestNowMs(when);
        layer.tick();
    }

    std::array<uint8_t, 4> pixel(size_t index) const {
        const uint8_t* p = layer.buffer().data() + index * 4;
        return {p[0], p[1], p[2], p[3]};
    }
};

} // namespace

TEST_CASE("HELIO factory patterns cover every physical pixel of current S M L rings") {
    for (uint16_t count : {uint16_t{156}, uint16_t{258}, uint16_t{360}}) {
        RingFixture fixture(count);
        CHECK(fixture.factory.phase == mm::HelioFactoryTestEffect::Off);
        fixture.frame(1000);
        for (size_t i = 0; i < count; ++i) CHECK((fixture.pixel(i) == std::array<uint8_t, 4>{0, 0, 0, 0}));

        fixture.factory.phase = mm::HelioFactoryTestEffect::FullWhite;
        fixture.frame(1100);
        for (size_t i = 0; i < count; ++i) CHECK((fixture.pixel(i) == std::array<uint8_t, 4>{255, 255, 255, 255}));

        fixture.factory.phase = mm::HelioFactoryTestEffect::Chase;
        fixture.frame(1200);
        CHECK((fixture.pixel(0) == std::array<uint8_t, 4>{64, 64, 64, 64}));
        CHECK((fixture.pixel(1) == std::array<uint8_t, 4>{0, 0, 0, 0}));
        fixture.frame(1400);
        CHECK((fixture.pixel(0) == std::array<uint8_t, 4>{0, 0, 0, 0}));
        CHECK((fixture.pixel(1) == std::array<uint8_t, 4>{64, 64, 64, 64}));
        fixture.frame(1200 + static_cast<uint32_t>(count) * 200u);
        CHECK((fixture.pixel(0) == std::array<uint8_t, 4>{64, 64, 64, 64}));
        CHECK((fixture.pixel(count - 1) == std::array<uint8_t, 4>{0, 0, 0, 0}));
    }
}

TEST_CASE("HELIO factory channel flats and quarter white do not retain previous channels") {
    RingFixture fixture(156);
    const std::array<std::pair<uint8_t, std::array<uint8_t, 4>>, 6> phases{{
        {mm::HelioFactoryTestEffect::Red, {255, 0, 0, 0}},
        {mm::HelioFactoryTestEffect::Green, {0, 255, 0, 0}},
        {mm::HelioFactoryTestEffect::Blue, {0, 0, 255, 0}},
        {mm::HelioFactoryTestEffect::White, {0, 0, 0, 255}},
        {mm::HelioFactoryTestEffect::QuarterWhite, {64, 64, 64, 64}},
        {mm::HelioFactoryTestEffect::Off, {0, 0, 0, 0}},
    }};
    uint32_t now = 1000;
    for (const auto& [phase, expected] : phases) {
        fixture.factory.phase = phase;
        fixture.frame(now);
        CHECK(fixture.pixel(0) == expected);
        CHECK(fixture.pixel(77) == expected);
        CHECK(fixture.pixel(155) == expected);
        now += 200;
    }
}

TEST_CASE("HELIO factory pattern fails dark on a three-channel layer") {
    RingFixture fixture(156, 3);
    auto& buffer = fixture.layer.buffer();
    for (size_t i = 0; i < buffer.bytes(); ++i) buffer.data()[i] = 255;
    fixture.factory.phase = mm::HelioFactoryTestEffect::FullWhite;
    fixture.frame(1000);
    for (size_t i = 0; i < buffer.bytes(); ++i) CHECK(buffer.data()[i] == 0);
}
