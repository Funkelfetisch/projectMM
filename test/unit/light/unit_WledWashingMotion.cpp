// @module WledWashingMotion
#include "doctest.h"
#include "light/effects/WledWashingMotion.h"
#include "light/effects/WledRandomPalette.h"
#include "light/effects/WledSineMotion.h"
#include "light/effects/WledPlasmoidFrame.h"

TEST_CASE("Archived Plasmoid preserves color balance and clears old white during its 1D fade") {
    using F = mm::WledPlasmoidFrame;
    auto combined = F::add({200, 20, 100, 0}, {100, 30, 0, 100});
    CHECK(combined.r == 255); CHECK(combined.g == 42);
    CHECK(combined.b == 85); CHECK(combined.w == 85);
    mm::RGB palette[16] = {};
    F::Pixel pixels[] = {{255, 128, 1, 255}};
    F frame;
    frame.step(pixels, 1, palette, {0, 0, 0, 0}, 0, 128, 0, 0);
    CHECK(pixels[0].r == 207); CHECK(pixels[0].g == 104);
    CHECK(pixels[0].b == 0); CHECK(pixels[0].w == 0);
}

TEST_CASE("Archived Sine motion retains FastLED cubic wave and WLED phase arithmetic") {
    using S = mm::WledSineMotion;
    CHECK(S::advance(0, 15) == 0);
    CHECK(S::advance(0xffffffffu, 16) == 0);
    CHECK(S::advance(100, 128) == 108);
    CHECK(S::wave(0, 0, 128) == 0);
    CHECK(S::wave(0, 1, 128) == 40);
    CHECK(S::wave(0, 2, 128) == 128);
    CHECK(S::wave(0, 4, 128) == 255);
    CHECK(S::wave(0, 8, 128) == 0);
    CHECK(S::wave(63, 0, 0) == 126);
    CHECK(S::wave(64, 0, 0) == 128);
    CHECK(S::paletteIndex(32u * 65536u, 31) == 0);
    CHECK(S::paletteIndex(32u * 255u, 31) == 31);
}

TEST_CASE("WLED washing motion retains attack, pause and reverse boundaries") {
    using M = mm::WledWashingMotion;
    CHECK(M::velocity(0) == 0);
    CHECK(M::velocity(15) == 127);
    CHECK(M::velocity(75) == 127);
    CHECK(M::velocity(90) == 0);
    CHECK(M::velocity(127) == 0);
    CHECK(M::velocity(142) == -127);
    CHECK(M::velocity(217) == 0);
    CHECK(M::advance(0, 15u << 7, 128) == 677);
    CHECK(M::advance(0, 142u << 7, 128) == uint32_t(0u - 677u));
    CHECK(M::waveInput(0, 0, 0, 128) == 0);
    CHECK(M::waveInput(0, 8, 9, 128) == uint8_t(6u * 255u * 8u / 9u));
    CHECK(M::sine(0) == 128);
    CHECK(M::sine(64) == 255);
    CHECK(M::sine(128) == 126);
    CHECK(M::sine(192) == 0);
}

TEST_CASE("Archived random palette preserves endpoint and truncated transition") {
    using P = mm::WledRandomPalette;
    mm::RGB palette[16] = {{255,0,0}};
    auto middle = P::blend({255,1,0}, {0,1,255}, 2500);
    CHECK(middle.r == 127); CHECK(middle.g == 0); CHECK(middle.b == 127);
    auto start = P::sample(palette, 0);
    CHECK(start.r == 255); CHECK(start.g == 0); CHECK(start.b == 0);
    auto finish = P::blend({10,20,30}, {40,50,60}, 7000);
    CHECK(finish.r == 40); CHECK(finish.g == 50); CHECK(finish.b == 60);
}

TEST_CASE("FastLED HSV palette follows shortest hue path across the wrap") {
    using P = mm::WledRandomPalette;
    P::HSV colors[4] = {{250, 255, 255}, {5, 255, 255}, {96, 255, 255}, {160, 255, 255}};
    mm::RGB palette[16];
    P::gradient(colors, palette);
    auto middle = P::rainbow({uint8_t(254), 255, 255});
    CHECK(palette[2].r == middle.r);
    CHECK(palette[2].g == middle.g);
    CHECK(palette[2].b == middle.b);
    auto white = P::rainbow({160, 0, 255});
    CHECK(white.r == 255); CHECK(white.g == 255); CHECK(white.b == 255);
    auto black = P::rainbow({160, 255, 0});
    CHECK(black.r == 0); CHECK(black.g == 0); CHECK(black.b == 0);
}

TEST_CASE("WLED palette 74 retains the asymmetric 250 ms transition") {
    mm::RGB old[16] = {}, next[16] = {}, out[16] = {};
    old[0] = {0, 255, 4}; next[0] = {255, 0, 4};
    mm::WledRandomPalette::transition74(old, next, 0, out);
    CHECK(out[0].r == 0); CHECK(out[0].g == 255); CHECK(out[0].b == 4);
    mm::WledRandomPalette::transition74(old, next, 250, out);
    CHECK(out[0].r == 128); CHECK(out[0].g == 0); CHECK(out[0].b == 4);
    mm::WledRandomPalette::transition74(old, next, 251, out);
    CHECK(out[0].r == 255); CHECK(out[0].g == 0); CHECK(out[0].b == 4);
}
