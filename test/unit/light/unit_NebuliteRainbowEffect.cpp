/// @module NebuliteRainbowEffect
#include "doctest.h"
#include "light/effects/NebuliteRainbowEffect.h"
#include "light/effects/PlasmaEffect.h"
#include "light/layouts/GridLayout.h"
#include "light/layouts/PixelMapLayout.h"
#include "helio/BlePixelMapPage.h"
#include "light/layouts/Layouts.h"
#include <string>
#include <vector>

TEST_CASE("Legacy rainbow wheel keeps WLED spatial scale and 16-bit clock") {
    using E = mm::NebuliteRainbowEffect;
    CHECK(E::wheelIndex(0, 128, 128, 0, 9) == 0);
    CHECK(E::wheelIndex(0, 128, 128, 8, 9) == 227);
    CHECK(E::wheelIndex(0, 128, 128, 30, 31) == 247);
    CHECK(E::wheelIndex(1, 128, 128, 0, 9) == 0);
    CHECK(E::wheelIndex(8, 128, 128, 0, 9) == 1);
    CHECK(E::wheelIndex(65536, 128, 128, 0, 9) == 0);
    CHECK(E::wheelIndex(99, 255, 255, 0, 0) == 0);
}

TEST_CASE("Legacy rainbow motion matches the archived WLED formula across wrap and sizes") {
    // WLED mode_rainbow_cycle uses this two-stage uint16 phase calculation.
    for (unsigned count : {1u, 9u, 31u, 91u, 222u})
        for (unsigned speed : {0u, 1u, 128u, 255u})
            for (unsigned size : {0u, 28u, 29u, 128u, 255u})
                for (uint32_t now : {0u, 7u, 1999u, 65535u, 0xffffffffu}) {
                    uint16_t counter = (now * ((speed >> 2) + 2)) & 0xffff;
                    counter >>= 8;
                    for (unsigned i = 0; i < count; ++i) {
                        uint8_t expected = (i * (16 << (size / 29)) / count) + counter;
                        CHECK(mm::NebuliteRainbowEffect::wheelIndex(now, speed, size, i, count) == expected);
                    }
                }
}

TEST_CASE("Legacy rainbow uses FastLED palette endpoints and fixed interpolation") {
    using E = mm::NebuliteRainbowEffect;
    auto red = E::rainbowColor(0);
    CHECK(red.r == 255); CHECK(red.g == 0); CHECK(red.b == 0);
    auto green = E::rainbowColor(96);
    CHECK(green.r == 0); CHECK(green.g == 255); CHECK(green.b == 0);
    auto mid = E::rainbowColor(8);
    CHECK(mid.r == 234); CHECK(mid.g == 21); CHECK(mid.b == 0);
    auto wrap = E::rainbowColor(248);
    CHECK(wrap.r == 234); CHECK(wrap.g == 0); CHECK(wrap.b == 21);
}

TEST_CASE("Legacy rainbow renders nine physical lights through a real RGBW layer") {
    mm::Layouts layouts;
    mm::GridLayout grid;
    grid.width = 9; grid.height = 1; grid.depth = 1;
    layouts.addChild(&grid);
    mm::Layer layer;
    layer.setLayouts(&layouts);
    layer.setChannelsPerLight(4);
    mm::NebuliteRainbowEffect effect;
    layer.addChild(&effect);
    layer.applyState();
    layer.tick();
    auto& frame = layer.buffer();
    REQUIRE(frame.count() == 9);
    REQUIRE(frame.data() != nullptr);
    for (size_t i = 0; i < frame.count(); ++i) {
        CHECK((frame.data()[4 * i] | frame.data()[4 * i + 1] | frame.data()[4 * i + 2]) != 0);
        CHECK(frame.data()[4 * i + 3] == 0);
    }
}

TEST_CASE("Legacy rainbow confines its single segment to the selected physical range") {
    mm::Layouts layouts;
    mm::GridLayout grid;
    grid.width = 91; grid.height = 1; grid.depth = 1;
    layouts.addChild(&grid);
    mm::Layer layer;
    layer.setLayouts(&layouts);
    layer.setChannelsPerLight(4);
    mm::NebuliteRainbowEffect effect;
    effect.segmentStart = 3;
    effect.segmentStop = 31;
    layer.addChild(&effect);
    layer.applyState();
    layer.tick();
    auto& frame = layer.buffer();
    REQUIRE(frame.count() == 91);
    REQUIRE(frame.data() != nullptr);
    for (size_t i = 0; i < frame.count(); ++i) {
        const uint8_t* c = frame.data() + 4 * i;
        CHECK(((c[0] | c[1] | c[2]) != 0) == (i >= 3 && i < 31));
        CHECK(c[3] == 0);
    }
    effect.segmentStart = 40; // An inverted range must be dark, never wrap.
    layer.tick();
    for (size_t i = 0; i < frame.bytes(); ++i) CHECK(frame.data()[i] == 0);
}

TEST_CASE("Wearable-sized 2D rasters render native MoonLight and retain physical-order legacy effects") {
    for (const auto [w, h] : {std::pair{3, 3}, std::pair{7, 13}}) {
        mm::Layouts layouts;
        mm::GridLayout grid; grid.width = w; grid.height = h; grid.depth = 1;
        layouts.addChild(&grid);
        mm::Layer layer; layer.setLayouts(&layouts); layer.setChannelsPerLight(4);
        mm::PlasmaEffect plasma; layer.addChild(&plasma); layer.applyState(); layer.tick();
        REQUIRE(layer.width() == w); REQUIRE(layer.height() == h);
        REQUIRE(layer.buffer().count() == size_t(w * h));
        const auto* pixels = layer.buffer().data(); REQUIRE(pixels != nullptr);
        bool variedY = false;
        for (int y = 1; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const auto at = size_t(y * w + x) * 4;
                const auto first = size_t(x) * 4;
                variedY |= pixels[at] != pixels[first] || pixels[at + 1] != pixels[first + 1] || pixels[at + 2] != pixels[first + 2];
            }
        CHECK(variedY);
    }
    // A separate layer uses the legacy physical-output address space rather than assuming the garment's 2D cell sequence is a physical strip.
    mm::Layouts layouts;
    mm::GridLayout grid; grid.width = 7; grid.height = 13; grid.depth = 1;
    layouts.addChild(&grid);
    mm::Layer layer; layer.setLayouts(&layouts); layer.setChannelsPerLight(4);
    mm::NebuliteRainbowEffect legacy; legacy.segmentStart = 31; legacy.segmentStop = 61;
    layer.addChild(&legacy); layer.applyState(); layer.tick();
    REQUIRE(layer.buffer().count() == 91); REQUIRE(layer.buffer().data() != nullptr);
    for (size_t i = 0; i < 91; ++i) {
        const auto* pixel = layer.buffer().data() + i * 4;
        CHECK(((pixel[0] | pixel[1] | pixel[2]) != 0) == (i >= 31 && i < 61));
    }
}

TEST_CASE("Pixel maps preserve output order and reject malformed or oversized coordinates") {
    mm::PixelMapLayout layout;
    std::strcpy(layout.points, "1,8;12,8;10,3;16,2;4,0;5,8;13,4;16,8;8,4");
    REQUIRE(layout.lightCount() == 9);
    std::vector<std::pair<int, int>> actual;
    layout.placeLights(mm::CoordSink{[](void* context, mm::nrOfLightsType index, mm::lengthType x, mm::lengthType y, mm::lengthType) {
        auto& points = *static_cast<std::vector<std::pair<int,int>>*>(context);
        CHECK(index == points.size()); points.emplace_back(x, y);
    }, nullptr, &actual});
    CHECK((actual == std::vector<std::pair<int,int>>{{1,8},{12,8},{10,3},{16,2},{4,0},{5,8},{13,4},{16,8},{8,4}}));
    for (const char* bad : {"", "1,2;", "1", "1,", "64,0", "0,64", "-1,0", "0,1;bad", "1,2\n"}) {
        CHECK_FALSE(mm::PixelMapLayout::validPoints(bad));
        std::strcpy(layout.points, bad); CHECK(layout.lightCount() == 0);
        actual.clear(); layout.placeLights(mm::CoordSink{[](void* context, mm::nrOfLightsType, mm::lengthType, mm::lengthType, mm::lengthType) {
            static_cast<std::vector<std::pair<int,int>>*>(context)->emplace_back(1,1);
        }, nullptr, &actual});
        CHECK(actual.empty());
    }
    std::string points;
    for (size_t i = 0; i < 128; ++i) { if (i) points += ';'; points += "0,0"; }
    CHECK(mm::PixelMapLayout::validPoints(points.c_str()));
    points += ";0,0";
    CHECK_FALSE(mm::PixelMapLayout::validPoints(points.c_str()));
}

TEST_CASE("BLE map pages refuse mixed revisions and fit the bounded response") {
    mm::PixelMapLayout map;
    std::strcpy(map.points, "5,14;7,14;9,14;11,14;13,14;15,14;5,6;7,6;9,6;11,6;13,6;15,6;7,2;8,1;9,1;11,1;12,1;13,2;0,5;1,6;2,7;1,9;2,10;3,11;20,5;19,6;18,7;19,9;18,10;17,11;10,10");
    char revision[33]; mm::sha256Hex(map.points, std::strlen(map.points), revision);
    char buf[512]{}; mm::JsonSink first(buf, sizeof(buf));
    first.append("{\"v\":1,\"id\":65535,\"e\":\"00112233445566778899aabbccddeeff\",\"s\":4294967295,\"r\":4294967295,");
    CHECK(mm::BlePixelMapPage::write(first, map, 0, nullptr) == nullptr);
    first.append("}"); CHECK_FALSE(first.overflowed());
    CHECK(std::strstr(buf, "\"total\":31,\"offset\":0") != nullptr);
    CHECK(std::strstr(buf, "\"next\":16") != nullptr);
    char nextBuf[512]{}; mm::JsonSink next(nextBuf, sizeof(nextBuf));
    CHECK(mm::BlePixelMapPage::write(next, map, 16, revision) == nullptr);
    CHECK(std::strstr(nextBuf, "\"next\":null") != nullptr);
    CHECK(std::strstr(nextBuf, "[10,10]") != nullptr);
    CHECK(std::strlen(nextBuf) < 400);
    std::strcpy(map.points, "6,14;7,14");
    char staleBuf[512]{}; mm::JsonSink stale(staleBuf, sizeof(staleBuf));
    CHECK(std::strcmp(mm::BlePixelMapPage::write(stale, map, 1, revision), "map-changed") == 0);
    CHECK(std::strcmp(mm::BlePixelMapPage::write(stale, map, 16, revision), "map-changed") == 0);
    CHECK(staleBuf[0] == '\0');
    char badBuf[512]{}; mm::JsonSink bad(badBuf, sizeof(badBuf));
    mm::sha256Hex(map.points, std::strlen(map.points), revision);
    CHECK(std::strcmp(mm::BlePixelMapPage::write(bad, map, 2, revision), "map-offset") == 0);
    CHECK(std::strcmp(mm::BlePixelMapPage::write(bad, map, 3, revision), "map-offset") == 0);
    std::strcpy(map.points, "1,2;bad");
    CHECK(std::strcmp(mm::BlePixelMapPage::write(bad, map, 0, nullptr), "map-invalid") == 0);
    std::string maximum;
    for (int i = 0; i < 128; ++i) { if (i) maximum += ';'; maximum += "63,63"; }
    std::strcpy(map.points, maximum.c_str());
    mm::sha256Hex(map.points, std::strlen(map.points), revision);
    for (uint16_t offset = 0; offset < 128; ++offset) {
        char bounded[512]{}; mm::JsonSink sink(bounded, sizeof(bounded));
        sink.append("{\"v\":1,\"id\":65535,\"now\":4294967295,\"e\":\"00112233445566778899aabbccddeeff\",\"s\":4294967295,\"r\":4294967295,\"op\":\"pixel-map\",\"ok\":true,");
        CHECK(mm::BlePixelMapPage::write(sink, map, offset, revision) == nullptr);
        sink.append("}"); CHECK_FALSE(sink.overflowed());
    }
    char endBuf[512]{}; mm::JsonSink end(endBuf, sizeof(endBuf));
    CHECK(std::strcmp(mm::BlePixelMapPage::write(end, map, 128, revision), "map-offset") == 0);
}
