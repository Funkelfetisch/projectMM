/// @module Control

/// A module's control list grows as it declares, and a control past what the list can hold is left out rather than written past its end.

#include "doctest.h"
#include "core/module/Control.h"
#include "core/module/MoonModule.h"

#include <cstdint>

// The doubling once wrapped a byte at 128 to a capacity of zero, so the 129th control wrote past the array.
TEST_CASE("a control list holds 255 controls and leaves out the ones after them") {
    mm::ControlList controls;
    static uint8_t values[300];
    for (int i = 0; i < 300; i++) controls.addControl("v", values[i]);
    CHECK(controls.count() == 255);
    CHECK(controls[254].ptr == &values[254]);
}

TEST_CASE("a list sized to its controls holds them all and still grows when one more is declared") {
    mm::ControlList controls;
    static uint8_t values[6];
    for (auto& v : values) controls.addControl("v", v);
    CHECK(controls.capacity() == 8);   // the doubling's 4, then 8
    controls.shrinkToFit();
    CHECK(controls.capacity() == 6);
    for (int i = 0; i < 6; i++) CHECK(controls[i].ptr == &values[i]);
    static uint8_t extra = 0;
    controls.addControl("extra", extra);
    CHECK(controls.count() == 7);
    CHECK(controls[6].ptr == &extra);
    controls.clear();
    controls.shrinkToFit();
    CHECK(controls.capacity() == 0);
}

TEST_CASE("each descriptor flag is its own bit, so setting one leaves the others as they were") {
    mm::ControlDescriptor d;
    d.hidden = true; d.hex = true; d.minMode = mm::kModeDeveloper;
    // Read into values: a bit has no address for the check to bind.
    const bool hidden = d.hidden, hex = d.hex, readonly = d.readonly, live = d.live, hosts = d.hosts;
    const uint8_t mode = d.minMode;
    CHECK(hidden);
    CHECK(hex);
    CHECK(mode == mm::kModeDeveloper);
    CHECK_FALSE(readonly);
    CHECK_FALSE(live);
    CHECK_FALSE(hosts);
    d.hidden = false;
    const bool hexAfter = d.hex;
    const uint8_t modeAfter = d.minMode;
    CHECK(hexAfter);
    CHECK(modeAfter == mm::kModeDeveloper);
}

namespace {
// Five controls, so the doubling leaves three empty slots until the rebuild fits the array.
struct FiveControls : mm::MoonModule {
    uint8_t v[5] = {};
    void defineControls() override {
        for (auto& x : v) controls_.addControl("v", x);
        MoonModule::defineControls();
    }
};
}  // namespace

TEST_CASE("a rebuild sizes every control array in the tree to the controls it declared") {
    FiveControls parent;
    auto* child = new FiveControls();
    parent.addChild(child);
    parent.rebuildControls();
    CHECK(parent.controls().count() == 5);
    CHECK(parent.controls().capacity() == 5);
    CHECK(child->controls().capacity() == 5);
    // A second rebuild declares the same set into the fitted array without growing it.
    parent.rebuildControls();
    CHECK(parent.controls().capacity() == 5);
    CHECK(parent.controls()[4].ptr == &parent.v[4]);
}
