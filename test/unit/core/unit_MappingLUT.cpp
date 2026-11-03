/// @module MappingLUT

#include "doctest.h"
#include "light/layers/MappingLUT.h"
#include "platform/platform.h"

#include <vector>

// Restores the maxAllocBlock cap to 0 (unlimited) on scope exit so a forced-paging case can't leak the cap into later tests.
struct BlockCapGuard {
    ~BlockCapGuard() { mm::platform::setTestMaxAllocBlock(0); }
};

// A fresh LUT carries no mapping (hasLUT==false, logicalCount==0); BlendMap takes the fast identity copy path.
TEST_CASE("MappingLUT default is identity (no LUT)") {
    mm::MappingLUT lut;
    CHECK(!lut.hasLUT());
    CHECK(lut.logicalCount() == 0);
}

// setIdentity(N) declares a 1:1 mapping for N lights without allocating a LUT; forEachDestination still iterates correctly.
TEST_CASE("MappingLUT setIdentity") {
    mm::MappingLUT lut;
    lut.setIdentity(256);
    CHECK(!lut.hasLUT());
    CHECK(lut.logicalCount() == 256);

    // forEachDestination in identity mode returns the logical index
    std::vector<mm::nrOfLightsType> dests;
    lut.forEachDestination(42, [&](mm::nrOfLightsType idx) { dests.push_back(idx); });
    REQUIRE(dests.size() == 1);
    CHECK(dests[0] == 42);
}

// Each logical light can map to a different count of physical lights; forEachDestination yields every mapped index in order.
TEST_CASE("MappingLUT 1:N mapping") {
    mm::MappingLUT lut;
    // 4 logical lights, each maps to different number of physical lights
    CHECK(lut.build(4, 10));
    CHECK_FALSE(!lut.hasLUT());

    // Logical 0 → physical {0, 3}
    mm::nrOfLightsType map0[] = {0, 3};
    lut.setMapping(0, map0, 2);

    // Logical 1 → physical {1, 2, 5, 7}
    mm::nrOfLightsType map1[] = {1, 2, 5, 7};
    lut.setMapping(1, map1, 4);

    // Logical 2 → physical {4}
    mm::nrOfLightsType map2[] = {4};
    lut.setMapping(2, map2, 1);

    // Logical 3 → physical {6, 8, 9}
    mm::nrOfLightsType map3[] = {6, 8, 9};
    lut.setMapping(3, map3, 3);

    lut.finalize();

    CHECK(lut.logicalCount() == 4);
    CHECK(lut.destinationCount() == 10);

    // Verify each logical index maps correctly
    std::vector<mm::nrOfLightsType> dests;

    dests.clear();
    lut.forEachDestination(0, [&](mm::nrOfLightsType idx) { dests.push_back(idx); });
    REQUIRE(dests.size() == 2);
    CHECK(dests[0] == 0);
    CHECK(dests[1] == 3);

    dests.clear();
    lut.forEachDestination(1, [&](mm::nrOfLightsType idx) { dests.push_back(idx); });
    REQUIRE(dests.size() == 4);
    CHECK(dests[0] == 1);
    CHECK(dests[1] == 2);
    CHECK(dests[2] == 5);
    CHECK(dests[3] == 7);

    dests.clear();
    lut.forEachDestination(2, [&](mm::nrOfLightsType idx) { dests.push_back(idx); });
    REQUIRE(dests.size() == 1);
    CHECK(dests[0] == 4);

    dests.clear();
    lut.forEachDestination(3, [&](mm::nrOfLightsType idx) { dests.push_back(idx); });
    REQUIRE(dests.size() == 3);
    CHECK(dests[0] == 6);
    CHECK(dests[1] == 8);
    CHECK(dests[2] == 9);
}

// When no single contiguous block fits (forced via the test cap) but total heap allows it, build() pages the destinations array. The mapping must read back identically to a single-alloc build, paging is an allocation detail, not a behavior change. isPaged() confirms the fallback actually engaged.
TEST_CASE("MappingLUT paged destinations read back identically") {
    BlockCapGuard guard;
    // Cap the largest block well below a 5000-destination array (5000 × sizeof(nrOfLightsType)) so tier-1 fails and tier-2 pages.
    mm::platform::setTestMaxAllocBlock(mm::MappingLUT::kPageEntries * sizeof(mm::nrOfLightsType));

    mm::MappingLUT lut;
    REQUIRE(lut.build(3, 5000));
    CHECK(lut.isPaged());  // the fallback engaged

    // Logical 1's run spans well past one 4096-entry page → exercises the page-boundary walk in forEachDestination. Build runs of known values.
    std::vector<mm::nrOfLightsType> run0, run1, run2;
    for (mm::nrOfLightsType i = 0; i < 100; i++) run0.push_back(i);
    for (mm::nrOfLightsType i = 0; i < 4000; i++) run1.push_back(static_cast<mm::nrOfLightsType>(1000 + i)); // crosses the 4096 page boundary
    for (mm::nrOfLightsType i = 0; i < 50; i++) run2.push_back(static_cast<mm::nrOfLightsType>(9000 + i));

    lut.setMapping(0, run0.data(), static_cast<mm::nrOfLightsType>(run0.size()));
    lut.setMapping(1, run1.data(), static_cast<mm::nrOfLightsType>(run1.size()));
    lut.setMapping(2, run2.data(), static_cast<mm::nrOfLightsType>(run2.size()));
    lut.finalize();

    auto collect = [&](mm::nrOfLightsType li) {
        std::vector<mm::nrOfLightsType> out;
        lut.forEachDestination(li, [&](mm::nrOfLightsType idx) { out.push_back(idx); });
        return out;
    };
    CHECK(collect(0) == run0);
    CHECK(collect(1) == run1);  // the boundary-straddling run round-trips exactly
    CHECK(collect(2) == run2);
    CHECK(lut.destinationCount() == run0.size() + run1.size() + run2.size());
}

// build() returns false on genuine exhaustion, total free heap (minus the reserve) can't hold the destinations, so the caller degrades to 1:1. Forced here via a non-zero freeHeap is desktop-only-unavailable, so this case pins the paged path's success and the boundary; the tier-3 false path is covered by the Layer sparse-mapping degrade test on real heap limits.
TEST_CASE("MappingLUT single-block path when a block fits (not paged)") {
    BlockCapGuard guard;
    mm::platform::setTestMaxAllocBlock(1 << 20);  // 1 MB block — fits any small LUT
    mm::MappingLUT lut;
    REQUIRE(lut.build(1, 10));
    CHECK_FALSE(lut.isPaged());  // single contiguous block taken
    mm::nrOfLightsType m[] = {0, 3};
    lut.setMapping(0, m, 2);
    lut.finalize();
    std::vector<mm::nrOfLightsType> out;
    lut.forEachDestination(0, [&](mm::nrOfLightsType idx) { out.push_back(idx); });
    REQUIRE(out.size() == 2);
    CHECK(out[0] == 0);
    CHECK(out[1] == 3);
}

// free() releases memory and resets counts; build() can be called again to install a fresh mapping.
TEST_CASE("MappingLUT free and rebuild") {
    mm::MappingLUT lut;
    lut.build(4, 8);
    lut.free();
    CHECK(!lut.hasLUT());
    CHECK(lut.logicalCount() == 0);

    // Can rebuild after free
    CHECK(lut.build(2, 4));
    mm::nrOfLightsType map[] = {0, 1};
    lut.setMapping(0, map, 2);
    lut.setMapping(1, map, 2);
    lut.finalize();
    CHECK(lut.logicalCount() == 2);
}

namespace {

struct MappingAllocationGuard {
    ~MappingAllocationGuard() {
        mm::platform::setTestHeapCapacity(0);
        mm::platform::setTestAllocRefusedFrom(0);
        mm::platform::setTestMaxAllocBlock(0);
        mm::platform::setTestFreeHeap(0);
        mm::platform::setTestFreeInternalHeap(0);
    }
};

std::vector<mm::nrOfLightsType> mappedDestinations(const mm::MappingLUT& lut, mm::nrOfLightsType logical) {
    std::vector<mm::nrOfLightsType> result;
    lut.forEachDestination(logical, [&](mm::nrOfLightsType physical) { result.push_back(physical); });
    return result;
}

}

TEST_CASE("MappingLUT emitter groups unordered destinations and preserves each run's order") {
    mm::MappingLUT lut;
    REQUIRE(lut.build(6, 7, [](auto visit) {
        visit(4, 91); visit(1, 7); visit(4, 30); visit(0, 12);
        visit(1, 2); visit(4, 88); visit(5, 41);
    }));
    CHECK(lut.hasLUT());
    CHECK(lut.destinationCount() == 7);
    CHECK(mappedDestinations(lut, 0) == std::vector<mm::nrOfLightsType>{12});
    CHECK(mappedDestinations(lut, 1) == std::vector<mm::nrOfLightsType>{7, 2});
    CHECK(mappedDestinations(lut, 2).empty());
    CHECK(mappedDestinations(lut, 3).empty());
    CHECK(mappedDestinations(lut, 4) == std::vector<mm::nrOfLightsType>{91, 30, 88});
    CHECK(mappedDestinations(lut, 5) == std::vector<mm::nrOfLightsType>{41});
}

TEST_CASE("MappingLUT emitter retains an empty nonidentity map for black gaps") {
    mm::MappingLUT lut;
    REQUIRE(lut.build(32, 32, [](auto) {}));
    CHECK(lut.hasLUT());
    CHECK(lut.logicalCount() == 32);
    CHECK(lut.destinationCount() == 0);
    for (mm::nrOfLightsType i = 0; i < 32; ++i) CHECK(mappedDestinations(lut, i).empty());
    CHECK(lut.memoryUsed() == 33 * sizeof(mm::nrOfLightsType));
}

TEST_CASE("MappingLUT emitter reads paged runs exactly and allocates only their actual tail") {
    MappingAllocationGuard restore;
    mm::platform::setTestMaxAllocBlock(mm::MappingLUT::kPageEntries * sizeof(mm::nrOfLightsType));
    const size_t before = mm::platform::allocatedBytes();
    {
        mm::MappingLUT lut;
        REQUIRE(lut.build(5, 6000, [](auto visit) {
            for (mm::nrOfLightsType i = 0; i < 5001; ++i) visit(i % 7 == 0 ? 0 : 2, i);
        }));
        CHECK(lut.isPaged());
        CHECK(lut.destinationCount() == 5001);
        CHECK(mm::platform::allocatedBytes() - before == (6 + 5001) * sizeof(mm::nrOfLightsType));
        std::vector<mm::nrOfLightsType> first, second;
        for (mm::nrOfLightsType i = 0; i < 5001; ++i) (i % 7 == 0 ? first : second).push_back(i);
        CHECK(mappedDestinations(lut, 0) == first);
        CHECK(mappedDestinations(lut, 1).empty());
        CHECK(mappedDestinations(lut, 2) == second);
        CHECK(mappedDestinations(lut, 3).empty());
        CHECK(mappedDestinations(lut, 4).empty());
    }
    CHECK(mm::platform::allocatedBytes() == before);
}

TEST_CASE("MappingLUT emitter releases offset and destination failures and recovers") {
    MappingAllocationGuard restore;
    const size_t before = mm::platform::allocatedBytes();
    auto emit = [](auto visit) {
        for (mm::nrOfLightsType i = 0; i < 100; ++i) visit(i % 2, i);
    };
    mm::MappingLUT lut;
    for (size_t refused : {size_t{1}, size_t{64}}) {
        mm::platform::setTestAllocRefusedFrom(0);
        REQUIRE(lut.build(2, 100, emit));
        mm::platform::setTestAllocRefusedFrom(refused);
        CHECK_FALSE(lut.build(3, 100, emit));
        CHECK(lut.logicalCount() == 0);
        CHECK(lut.destinationCount() == 0);
        CHECK(lut.memoryUsed() == 0);
        CHECK(mm::platform::allocatedBytes() == before);
    }
    mm::platform::setTestAllocRefusedFrom(0);
    REQUIRE(lut.build(2, 100, emit));
    const auto even = mappedDestinations(lut, 0);
    const auto odd = mappedDestinations(lut, 1);
    REQUIRE(even.size() == 50);
    REQUIRE(odd.size() == 50);
    for (size_t i = 0; i < 50; ++i) {
        CHECK(even[i] == 2 * i);
        CHECK(odd[i] == 2 * i + 1);
    }
    lut.free();
    CHECK(mm::platform::allocatedBytes() == before);
}

TEST_CASE("MappingLUT emitter closes the last logical cell at the uint16 boundary") {
    mm::MappingLUT lut;
    REQUIRE(lut.build(65535, 3, [](auto visit) {
        visit(65534, 19); visit(0, 3); visit(65534, 21);
    }));
    CHECK(lut.logicalCount() == 65535);
    CHECK(lut.destinationCount() == 3);
    CHECK(mappedDestinations(lut, 0) == std::vector<mm::nrOfLightsType>{3});
    CHECK(mappedDestinations(lut, 65533).empty());
    CHECK(mappedDestinations(lut, 65534) == std::vector<mm::nrOfLightsType>{19, 21});
}

TEST_CASE("MappingLUT emitter refuses capacity overflow and invalid logical destinations without leaks") {
    const size_t before = mm::platform::allocatedBytes();
    mm::MappingLUT lut;
    CHECK_FALSE(lut.build(2, 10, [](auto visit) {
        for (mm::nrOfLightsType i = 0; i < 11; ++i) visit(i % 2, i);
    }));
    CHECK(lut.logicalCount() == 0);
    CHECK(mm::platform::allocatedBytes() == before);
    CHECK_FALSE(lut.build(2, 10, [](auto visit) { visit(2, 1); }));
    CHECK(lut.logicalCount() == 0);
    CHECK(mm::platform::allocatedBytes() == before);
}

TEST_CASE("MappingLUT reserve applies to flat offsets and destinations in both builders") {
    MappingAllocationGuard restore;
    mm::platform::setTestMaxAllocBlock(1 << 20);
    const size_t before = mm::platform::allocatedBytes();
    const size_t offsetBytes = 3 * sizeof(mm::nrOfLightsType);
    const size_t destinationBytes = 8 * sizeof(mm::nrOfLightsType);
    auto emit = [](auto visit) {
        for (mm::nrOfLightsType i = 0; i < 8; ++i) visit(i % 2, i);
    };
    for (bool emitter : {false, true}) {
        for (size_t headroom : {offsetBytes - 1, destinationBytes}) {
            CAPTURE(emitter);
            CAPTURE(headroom);
            mm::MappingLUT lut;
            mm::platform::setTestFreeHeap(mm::platform::HEAP_RESERVE + headroom);
            const bool built = emitter ? lut.build(2, 8, emit) : lut.build(2, 8);
            CHECK_FALSE(built);
            CHECK(lut.logicalCount() == 0);
            CHECK(lut.memoryUsed() == 0);
            CHECK(mm::platform::allocatedBytes() == before);
        }
    }
}

TEST_CASE("MappingLUT reserve accepts an exact cumulative boundary and preserves mapped values") {
    MappingAllocationGuard restore;
    const size_t before = mm::platform::allocatedBytes();
    constexpr size_t bytes = (3 + 8) * sizeof(mm::nrOfLightsType);
    for (bool emitter : {false, true}) {
        CAPTURE(emitter);
        mm::platform::setTestFreeHeap(mm::platform::HEAP_RESERVE + bytes);
        mm::MappingLUT lut;
        if (emitter) {
            REQUIRE(lut.build(2, 8, [](auto visit) {
                for (mm::nrOfLightsType i = 0; i < 8; ++i) visit(i % 2, i);
            }));
        } else {
            REQUIRE(lut.build(2, 8));
            const mm::nrOfLightsType even[] = {0, 2, 4, 6}, odd[] = {1, 3, 5, 7};
            lut.setMapping(0, even, 4);
            lut.setMapping(1, odd, 4);
            lut.finalize();
        }
        CHECK_FALSE(lut.isPaged());
        CHECK(mappedDestinations(lut, 0) == std::vector<mm::nrOfLightsType>{0, 2, 4, 6});
        CHECK(mappedDestinations(lut, 1) == std::vector<mm::nrOfLightsType>{1, 3, 5, 7});
        CHECK(mm::platform::allocatedBytes() - before == bytes);
    }
    CHECK(mm::platform::allocatedBytes() == before);
}

TEST_CASE("MappingLUT emitter budgets real destinations rather than the unused capacity ceiling") {
    MappingAllocationGuard restore;
    const size_t before = mm::platform::allocatedBytes();
    constexpr size_t bytes = (33 + 2) * sizeof(mm::nrOfLightsType);
    mm::platform::setTestFreeHeap(mm::platform::HEAP_RESERVE + bytes);
    mm::MappingLUT lut;
    REQUIRE(lut.build(32, 6000, [](auto visit) { visit(31, 41); visit(3, 72); }));
    CHECK(lut.destinationCount() == 2);
    CHECK(mappedDestinations(lut, 3) == std::vector<mm::nrOfLightsType>{72});
    CHECK(mappedDestinations(lut, 31) == std::vector<mm::nrOfLightsType>{41});
    CHECK(mm::platform::allocatedBytes() - before == bytes);
}

TEST_CASE("MappingLUT paged reserve is cumulative and a refused build releases all pages before retry") {
    MappingAllocationGuard restore;
    mm::platform::setTestMaxAllocBlock(mm::MappingLUT::kPageEntries * sizeof(mm::nrOfLightsType));
    const size_t before = mm::platform::allocatedBytes();
    constexpr size_t bytes = (4 + 5001) * sizeof(mm::nrOfLightsType);
    auto emit = [](auto visit) {
        for (mm::nrOfLightsType i = 0; i < 5001; ++i) visit(2, i);
    };
    for (bool emitter : {false, true}) {
        CAPTURE(emitter);
        mm::MappingLUT lut;
        mm::platform::setTestFreeHeap(mm::platform::HEAP_RESERVE + bytes - 1);
        const bool refused = emitter ? lut.build(3, 5001, emit) : lut.build(3, 5001);
        CHECK_FALSE(refused);
        CHECK(lut.logicalCount() == 0);
        CHECK(mm::platform::allocatedBytes() == before);
        mm::platform::setTestFreeHeap(mm::platform::HEAP_RESERVE + bytes);
        const bool rebuilt = emitter ? lut.build(3, 5001, emit) : lut.build(3, 5001);
        REQUIRE(rebuilt);
        CHECK(lut.isPaged());
        if (!emitter) {
            std::vector<mm::nrOfLightsType> destinations;
            for (mm::nrOfLightsType i = 0; i < 5001; ++i) destinations.push_back(i);
            lut.setMapping(0, nullptr, 0);
            lut.setMapping(1, nullptr, 0);
            lut.setMapping(2, destinations.data(), 5001);
            lut.finalize();
        }
        const auto destinations = mappedDestinations(lut, 2);
        REQUIRE(destinations.size() == 5001);
        for (size_t i = 0; i < destinations.size(); ++i) CHECK(destinations[i] == i);
        CHECK(mm::platform::allocatedBytes() - before == bytes);
        lut.free();
        CHECK(mm::platform::allocatedBytes() == before);
    }
}

TEST_CASE("MappingLUT releases the old map before admitting a replacement") {
    MappingAllocationGuard restore;
    const size_t before = mm::platform::allocatedBytes();
    mm::MappingLUT lut;
    REQUIRE(lut.build(128, 128, [](auto visit) {
        for (mm::nrOfLightsType i = 0; i < 128; ++i) visit(i, i);
    }));
    constexpr size_t bytes = (5 + 2) * sizeof(mm::nrOfLightsType);
    mm::platform::setTestFreeHeap(mm::platform::HEAP_RESERVE + bytes);
    unsigned passes = 0;
    const bool built = lut.build(4, 2, [&](auto visit) {
        if (++passes == 1) CHECK(mm::platform::allocatedBytes() - before == 5 * sizeof(mm::nrOfLightsType));
        visit(1, 44); visit(3, 98);
    });
    REQUIRE(built);
    CHECK(mappedDestinations(lut, 1) == std::vector<mm::nrOfLightsType>{44});
    CHECK(mappedDestinations(lut, 3) == std::vector<mm::nrOfLightsType>{98});
    CHECK(mm::platform::allocatedBytes() - before == bytes);
}

TEST_CASE("MappingLUT emitter honors a higher reserve and clamps a lower reserve to the platform floor") {
    MappingAllocationGuard restore;
    const size_t before = mm::platform::allocatedBytes();
    constexpr size_t bytes = (3 + 2) * sizeof(mm::nrOfLightsType);
    auto emit = [](auto visit) { visit(0, 12); visit(1, 19); };
    for (size_t requestedReserve : {size_t{0}, mm::platform::HEAP_RESERVE + 4096}) {
        const size_t requiredReserve = requestedReserve ? requestedReserve : mm::platform::HEAP_RESERVE;
        CAPTURE(requestedReserve);
        mm::MappingLUT lut;
        mm::platform::setTestFreeHeap(requiredReserve + bytes - 1);
        CHECK_FALSE(lut.build(2, 2, emit, requestedReserve));
        CHECK(mm::platform::allocatedBytes() == before);
        mm::platform::setTestFreeHeap(requiredReserve + bytes);
        REQUIRE(lut.build(2, 2, emit, requestedReserve));
        CHECK(mappedDestinations(lut, 0) == std::vector<mm::nrOfLightsType>{12});
        CHECK(mappedDestinations(lut, 1) == std::vector<mm::nrOfLightsType>{19});
    }
    CHECK(mm::platform::allocatedBytes() == before);
}

TEST_CASE("MappingLUT emitter refuses newly lost headroom between count and scatter and later recovers") {
    MappingAllocationGuard restore;
    const size_t before = mm::platform::allocatedBytes();
    constexpr size_t destinationBytes = 8 * sizeof(mm::nrOfLightsType);
    constexpr size_t bytes = 3 * sizeof(mm::nrOfLightsType) + destinationBytes;
    mm::MappingLUT lut;
    mm::platform::setTestFreeHeap(mm::platform::HEAP_RESERVE + bytes);
    unsigned calls = 0;
    CHECK_FALSE(lut.build(2, 8, [&](auto visit) {
        for (mm::nrOfLightsType i = 0; i < 8; ++i) visit(i % 2, i);
        if (++calls == 1) mm::platform::setTestFreeHeap(mm::platform::HEAP_RESERVE + destinationBytes - 1);
    }));
    CHECK(lut.logicalCount() == 0);
    CHECK(mm::platform::allocatedBytes() == before);
    mm::platform::setTestFreeHeap(mm::platform::HEAP_RESERVE + bytes);
    REQUIRE(lut.build(2, 8, [](auto visit) {
        for (mm::nrOfLightsType i = 0; i < 8; ++i) visit(i % 2, i);
    }));
    CHECK(mappedDestinations(lut, 0) == std::vector<mm::nrOfLightsType>{0, 2, 4, 6});
}

TEST_CASE("MappingLUT builders reuse admitted storage below reserve without another allocation") {
    MappingAllocationGuard restore;
    for (bool emitter : {false, true}) {
        CAPTURE(emitter);
        mm::platform::setTestFreeHeap(0);
        mm::platform::setTestFreeInternalHeap(0);
        mm::platform::setTestAllocRefusedFrom(0);
        mm::MappingLUT lut;
        auto emit = [](auto visit) {
            for (mm::nrOfLightsType i = 0; i < 8; ++i) visit(i % 2, i);
        };
        const bool initial = emitter ? lut.build(2, 8, emit) : lut.build(2, 8);
        REQUIRE(initial);
        const size_t bytes = mm::platform::allocatedBytes();
        const uint32_t blocks = mm::platform::allocatedCount();
        mm::platform::setTestFreeHeap(mm::platform::HEAP_RESERVE - 1);
        mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE - 1);
        mm::platform::setTestAllocRefusedFrom(1);
        const bool reused = emitter ? lut.build(2, 8, emit) : lut.build(2, 8);
        REQUIRE(reused);
        if (!emitter) {
            const mm::nrOfLightsType even[] = {0, 2, 4, 6}, odd[] = {1, 3, 5, 7};
            lut.setMapping(0, even, 4);
            lut.setMapping(1, odd, 4);
            lut.finalize();
        }
        CHECK(mappedDestinations(lut, 0) == std::vector<mm::nrOfLightsType>{0, 2, 4, 6});
        CHECK(mappedDestinations(lut, 1) == std::vector<mm::nrOfLightsType>{1, 3, 5, 7});
        CHECK(mm::platform::allocatedBytes() == bytes);
        CHECK(mm::platform::allocatedCount() == blocks);
    }
}

TEST_CASE("MappingLUT reused capacity remains accounted when a new map has fewer destinations") {
    MappingAllocationGuard restore;
    const size_t before = mm::platform::allocatedBytes();
    mm::MappingLUT lut;
    REQUIRE(lut.build(2, 8, [](auto visit) {
        for (mm::nrOfLightsType i = 0; i < 8; ++i) visit(i % 2, i);
    }));
    const size_t bytes = mm::platform::allocatedBytes();
    mm::platform::setTestFreeHeap(mm::platform::HEAP_RESERVE - 1);
    mm::platform::setTestAllocRefusedFrom(1);
    REQUIRE(lut.build(2, 8, [](auto visit) { visit(1, 97); visit(0, 38); }));
    CHECK(lut.destinationCount() == 2);
    CHECK(lut.memoryUsed() == bytes - before);
    CHECK(mm::platform::allocatedBytes() == bytes);
    CHECK(mappedDestinations(lut, 0) == std::vector<mm::nrOfLightsType>{38});
    CHECK(mappedDestinations(lut, 1) == std::vector<mm::nrOfLightsType>{97});
}

TEST_CASE("MappingLUT manual rebuild enforces its requested limit while retaining larger storage") {
    MappingAllocationGuard restore;
    mm::MappingLUT lut;
    REQUIRE(lut.build(2, 8));
    const size_t held = lut.memoryUsed();
    const size_t allocated = mm::platform::allocatedBytes();
    mm::platform::setTestFreeHeap(mm::platform::HEAP_RESERVE - 1);
    mm::platform::setTestAllocRefusedFrom(1);
    REQUIRE(lut.build(2, 1));
    const mm::nrOfLightsType destinations[] = {99, 98, 97, 96, 95, 94, 93, 92};
    lut.setMapping(0, destinations, 8);
    lut.setMapping(1, nullptr, 0);
    lut.finalize();
    CHECK(lut.destinationCount() == 1);
    CHECK(mappedDestinations(lut, 0) == std::vector<mm::nrOfLightsType>{99});
    CHECK(mappedDestinations(lut, 1).empty());
    CHECK(lut.memoryUsed() == held);
    CHECK(mm::platform::allocatedBytes() == allocated);
}

TEST_CASE("MappingLUT refuses fresh allocation when internal headroom is already below reserve") {
    MappingAllocationGuard restore;
    const size_t before = mm::platform::allocatedBytes();
    mm::platform::setTestFreeHeap(mm::platform::HEAP_RESERVE + 4096);
    mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE - 1);
    mm::platform::setTestMaxAllocBlock(1 << 20);
    for (bool emitter : {false, true}) {
        CAPTURE(emitter);
        mm::MappingLUT lut;
        const bool built = emitter ? lut.build(2, 1, [](auto visit) { visit(0, 3); }) : lut.build(2, 1);
        CHECK_FALSE(built);
        CHECK(mm::platform::allocatedBytes() == before);
    }
}

TEST_CASE("MappingLUT accepts exactly the currently available destination bytes after counting") {
    MappingAllocationGuard restore;
    constexpr size_t destinationBytes = 8 * sizeof(mm::nrOfLightsType);
    constexpr size_t totalBytes = 3 * sizeof(mm::nrOfLightsType) + destinationBytes;
    mm::platform::setTestFreeHeap(mm::platform::HEAP_RESERVE + totalBytes);
    mm::MappingLUT lut;
    unsigned passes = 0;
    REQUIRE(lut.build(2, 8, [&](auto visit) {
        for (mm::nrOfLightsType i = 0; i < 8; ++i) visit(i % 2, i);
        if (++passes == 1) mm::platform::setTestFreeHeap(mm::platform::HEAP_RESERVE + destinationBytes);
    }));
    CHECK(mappedDestinations(lut, 0) == std::vector<mm::nrOfLightsType>{0, 2, 4, 6});
    CHECK(mappedDestinations(lut, 1) == std::vector<mm::nrOfLightsType>{1, 3, 5, 7});
}

TEST_CASE("MappingLUT exact cumulative reserve also works with a heap that debits live allocations") {
    MappingAllocationGuard restore;
    const size_t before = mm::platform::allocatedBytes();
    for (mm::nrOfLightsType count : {mm::nrOfLightsType{8}, mm::nrOfLightsType{5001}}) {
        for (bool emitter : {false, true}) {
            CAPTURE(count);
            CAPTURE(emitter);
            const size_t bytes = (3 + static_cast<size_t>(count)) * sizeof(mm::nrOfLightsType);
            mm::platform::setTestHeapCapacity(before + mm::platform::HEAP_RESERVE + bytes);
            mm::platform::setTestMaxAllocBlock(mm::MappingLUT::kPageEntries * sizeof(mm::nrOfLightsType));
            mm::MappingLUT lut;
            const bool built = emitter ? lut.build(2, count, [&](auto visit) {
                for (mm::nrOfLightsType i = 0; i < count; ++i) visit(i % 2, i);
            }) : lut.build(2, count);
            REQUIRE(built);
            CHECK(lut.isPaged() == (count > mm::MappingLUT::kPageEntries));
            CHECK(mm::platform::allocatedBytes() - before == bytes);
            CHECK(mm::platform::freeHeap() == mm::platform::HEAP_RESERVE);
            CHECK(mm::platform::freeInternalHeap() == mm::platform::HEAP_RESERVE);
            if (emitter) {
                const auto odd = mappedDestinations(lut, 1);
                REQUIRE(odd.size() == static_cast<size_t>(count) / 2);
                CHECK(odd.back() == count - 1 - count % 2);
            }
        }
    }
    CHECK(mm::platform::allocatedBytes() == before);
}

TEST_CASE("MappingLUT replacement recovers old allocation credit before taking its new live-heap budget") {
    MappingAllocationGuard restore;
    const size_t before = mm::platform::allocatedBytes();
    mm::MappingLUT lut;
    REQUIRE(lut.build(128, 128, [](auto visit) {
        for (mm::nrOfLightsType i = 0; i < 128; ++i) visit(i, i);
    }));
    constexpr size_t bytes = (5 + 2) * sizeof(mm::nrOfLightsType);
    mm::platform::setTestHeapCapacity(before + mm::platform::HEAP_RESERVE + bytes);
    REQUIRE(mm::platform::freeHeap() < mm::platform::HEAP_RESERVE);
    REQUIRE(lut.build(4, 2, [](auto visit) { visit(1, 44); visit(3, 98); }));
    CHECK(mm::platform::freeHeap() == mm::platform::HEAP_RESERVE);
    CHECK(mappedDestinations(lut, 1) == std::vector<mm::nrOfLightsType>{44});
    CHECK(mappedDestinations(lut, 3) == std::vector<mm::nrOfLightsType>{98});
    mm::platform::setTestHeapCapacity(mm::platform::allocatedBytes() + mm::platform::HEAP_RESERVE - 1);
    mm::platform::setTestAllocRefusedFrom(1);
    REQUIRE(lut.build(4, 2, [](auto visit) { visit(2, 71); visit(0, 22); }));
    CHECK(mm::platform::freeHeap() == mm::platform::HEAP_RESERVE - 1);
    CHECK(mappedDestinations(lut, 0) == std::vector<mm::nrOfLightsType>{22});
    CHECK(mappedDestinations(lut, 2) == std::vector<mm::nrOfLightsType>{71});
}

TEST_CASE("desktop heap-capacity fixture reports exhaustion without masquerading as an unreported heap") {
    MappingAllocationGuard restore;
    const size_t before = mm::platform::allocatedBytes();
    mm::platform::setTestFreeHeap(123);
    mm::platform::setTestFreeInternalHeap(456);
    mm::platform::setTestHeapCapacity(before + 32);
    CHECK(mm::platform::freeHeap() == 32);
    CHECK(mm::platform::freeInternalHeap() == 32);
    void* allocation = mm::platform::alloc(64);
    REQUIRE(allocation != nullptr);
    CHECK(mm::platform::freeHeap() == 1);
    CHECK(mm::platform::freeInternalHeap() == 1);
    mm::platform::free(allocation);
    CHECK(mm::platform::freeHeap() == 32);
    mm::platform::setTestHeapCapacity(0);
    CHECK(mm::platform::freeHeap() == 123);
    CHECK(mm::platform::freeInternalHeap() == 456);
}

// A region moving every frame rebuilds the table each time, so a rebuild at about the same size keeps its blocks and maps as a fresh table does.
TEST_CASE("MappingLUT rebuild at the same size keeps its blocks") {
    BlockCapGuard guard;
    mm::platform::setTestMaxAllocBlock(1 << 20);
    mm::MappingLUT lut;
    REQUIRE(lut.build(4, 8));
    const size_t bytes = mm::platform::allocatedBytes();
    const uint32_t blocks = mm::platform::allocatedCount();
    lut.setOverwrites(false);

    REQUIRE(lut.build(4, 8));
    CHECK(mm::platform::allocatedBytes() == bytes);
    CHECK(mm::platform::allocatedCount() == blocks);
    CHECK(lut.overwrites());   // a rebuild starts as a fresh table
    mm::nrOfLightsType m[] = {2, 5};
    for (mm::nrOfLightsType i = 0; i < 4; i++) lut.setMapping(i, m, 2);
    lut.finalize();
    std::vector<mm::nrOfLightsType> out;
    lut.forEachDestination(3, [&](mm::nrOfLightsType idx) { out.push_back(idx); });
    REQUIRE(out.size() == 2);
    CHECK(out[1] == 5);

    REQUIRE(lut.build(40, 80));   // grown: allocated afresh
    CHECK(mm::platform::allocatedBytes() > bytes);
}

// A layer's fold meets its lights in wire order, not logical order, so the table is built in place: counted, turned into runs, then each light placed in its cell's run, with no copy of the table. It maps as one filled in logical order does, single block or paged.
TEST_CASE("MappingLUT built in place maps as one filled in logical order") {
    BlockCapGuard guard;
    for (const size_t cap : {size_t{1} << 20, size_t{16}}) {   // a single block, then pages: 20 bytes of destinations above a 16-byte block
        mm::platform::setTestMaxAllocBlock(cap);
        // Wire order (logical, physical): cell 2 first, cell 0 split around it, cell 1 empty.
        const std::vector<std::pair<mm::nrOfLightsType, mm::nrOfLightsType>> wire = {{2, 7}, {0, 3}, {2, 8}, {0, 4}, {3, 9}};
        mm::MappingLUT lut;
        REQUIRE(lut.build(4, 5, [&](auto emit) {
            for (const auto& [li, dest] : wire) emit(li, dest);
        }));

        CHECK(lut.hasLUT());
        CHECK(lut.isPaged() == (cap == 16));
        CHECK(lut.destinationCount() == 5);
        const std::vector<std::vector<mm::nrOfLightsType>> want = {{3, 4}, {}, {7, 8}, {9}};
        for (mm::nrOfLightsType li = 0; li < 4; li++) {
            std::vector<mm::nrOfLightsType> got;
            lut.forEachDestination(li, [&](mm::nrOfLightsType idx) { got.push_back(idx); });
            CHECK(got == want[li]);
        }
    }
}
