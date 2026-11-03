// @module WledBlinkTiming
#include "doctest.h"
#include "light/effects/WledBlinkTiming.h"

TEST_CASE("WLED blink preserves the old wearable's speed and duty boundaries") {
    mm::WledBlinkTiming phase;
    // speed 230 gives 500 ms plus two 24 ms frames; intensity 128
    // gives a 274 ms on-window, inclusive at its right edge.
    CHECK(phase.next(0, 230, 128, 24));
    CHECK(phase.next(274, 230, 128, 24));
    CHECK_FALSE(phase.next(275, 230, 128, 24));
    CHECK(phase.next(548, 230, 128, 24));
    CHECK_FALSE(phase.next(823, 230, 128, 24));
}

TEST_CASE("WLED blink displays one frame after skipped cycles") {
    mm::WledBlinkTiming phase;
    CHECK(phase.next(0, 230, 128, 24));
    CHECK(phase.next(548 * 2 + 400, 230, 128, 24));
    CHECK_FALSE(phase.next(548 * 2 + 401, 230, 128, 24));
    phase.reset();
    CHECK(phase.next(548 * 2 + 401, 230, 128, 24));
    CHECK_FALSE(phase.next(0, 255, 255, 0));
}

TEST_CASE("Archived wearable 42 FPS uses 23 ms in the WLED timing formula") {
    mm::WledBlinkTiming phase;
    // 500 ms base, 23 ms frame; the inclusive on-window ends at 273 ms.
    CHECK(phase.next(273, 230, 128, 23));
    CHECK_FALSE(phase.next(274, 230, 128, 23));
    CHECK(phase.next(546, 230, 128, 23));
}
