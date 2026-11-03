// @module WledTwoColorPalette
#include "doctest.h"
#include "light/effects/WledTwoColorPalette.h"

TEST_CASE("Legacy two-colour palette maps blue through black across a segment") {
    const mm::RGB blue{0, 0, 255}, black{0, 0, 0};
    auto sample = [&](unsigned pixel) { return mm::WledTwoColorPalette::sample(blue, black, pixel, 31, 0); };
    CHECK(sample(0).b == 255);
    // WLED maps pixel 15 to index 119 after no-wrap scaling: blend the
    // FastLED entries 7 and 8 at seven sixteenths.
    CHECK(sample(15).b == 131);
    CHECK(sample(30).b == 0);
    CHECK(sample(15).r == 0);
}

TEST_CASE("Legacy palette retains the wrap and no-blend options") {
    const mm::RGB blue{0, 0, 255}, black{0, 0, 0};
    CHECK(mm::WledTwoColorPalette::sample(blue, black, 30, 31, 1).b == 240);
    CHECK(mm::WledTwoColorPalette::sample(blue, black, 30, 31, 3).b == 0);
    CHECK(mm::WledTwoColorPalette::sample(blue, black, 0, 1, 0).b == 255);
}
