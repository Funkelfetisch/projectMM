/// @module RingLayout

#include "doctest.h"
#include "light/layouts/RingLayout.h"

#include <vector>

namespace {
struct RingPoint {
    mm::nrOfLightsType index;
    mm::lengthType x, y, z;
};

std::vector<RingPoint> points(const mm::RingLayout& ring) {
    std::vector<RingPoint> result;
    ring.placeLights(mm::CoordSink{[](void* ctx, mm::nrOfLightsType index,
                                     mm::lengthType x, mm::lengthType y, mm::lengthType z) {
        static_cast<std::vector<RingPoint>*>(ctx)->push_back({index, x, y, z});
    }, nullptr, &result});
    return result;
}
}

TEST_CASE("Ring preserves 222 and 311 consecutive physical lights") {
    mm::RingLayout ring;
    ring.radius = 24;
    for (uint16_t count : {uint16_t{222}, uint16_t{311}}) {
        ring.nrOfLEDs = count;
        CHECK(ring.lightCount() == count);
        const auto placed = points(ring);
        REQUIRE(placed.size() == count);
        for (mm::nrOfLightsType i = 0; i < count; i++) {
            CHECK(placed[i].index == i);
            CHECK(placed[i].z == 0);
        }
    }
}

TEST_CASE("Ring radius changes geometry without changing wiring") {
    mm::RingLayout ring;
    ring.nrOfLEDs = 311;
    ring.radius = 24;
    const auto fixed = points(ring);
    ring.radius = 0;
    const auto automatic = points(ring);
    REQUIRE(fixed.size() == 311);
    REQUIRE(automatic.size() == 311);
    CHECK(fixed[0].y != automatic[0].y);
    ring.nrOfLEDs = 0;
    CHECK(ring.lightCount() == 0);
    CHECK(points(ring).empty());
}
