/// @module ParticlesEffect

#include "doctest.h"
#include "light/layouts/Layouts.h"
#include "light/effects/ParticlesEffect.h"
#include "light/layouts/GridLayout.h"
#include "golden_frame.h"
#include <algorithm>

TEST_CASE("working memory: Particles releases an unusable pool and recovers") {
    struct Restore { ~Restore() { mm::platform::setTestAllocRefusedFrom(0); } } restore;
    mm::golden::ScopedTestClock clock(1000);
    mm::Layouts layouts; mm::GridLayout grid; mm::Layer layer; mm::ParticlesEffect effect;
    grid.width = 50; grid.height = 51; grid.depth = 1;
    layouts.addChild(&grid); layer.setLayouts(&layouts); layer.setChannelsPerLight(4);
    layer.prepare();
    layer.addChild(&effect);
    mm::platform::setTestAllocRefusedFrom(8000);
    effect.prepare();
    CHECK(effect.dynamicBytes() == 0);
    CHECK(effect.severity() == mm::MoonModule::Severity::Error);
    CHECK(effect.status() != nullptr);
    mm::platform::setTestAllocRefusedFrom(0);
    effect.prepare();
    CHECK(effect.dynamicBytes() > 0);
    CHECK(effect.status() == nullptr);
    for (uint16_t i = 0; i < 30; i++) { mm::platform::setTestNowMs(1000 + i * 20u); layer.tick(); }
    CHECK(std::any_of(layer.buffer().span().begin(), layer.buffer().span().end(),
                      [](uint8_t value) { return value != 0; }));
}

TEST_CASE("working memory: enabling released Particles reseeds the pool") {
    mm::golden::ScopedTestClock clock(1000);
    mm::Layouts layouts; mm::GridLayout grid; mm::Layer layer; mm::ParticlesEffect effect;
    grid.width = 16; grid.height = 16; grid.depth = 1;
    layouts.addChild(&grid); layer.setLayouts(&layouts); layer.setChannelsPerLight(3);
    layer.addChild(&effect); layer.applyState(); layer.tick();
    effect.release(); effect.prepare();
    mm::platform::setTestNowMs(1020); layer.tick();
    size_t lit = 0;
    const auto& buf = layer.buffer();
    for (size_t i = 0; i + 2 < buf.bytes(); i += 3) {
        if (buf.data()[i] || buf.data()[i + 1] || buf.data()[i + 2]) ++lit;
    }
    CHECK(lit > 1);
}

// The trail buffer sizes to width × height × 3 bytes (one RGB per cell, used to fade existing pixels).
TEST_CASE("ParticlesEffect allocates trail buffer when enabled") {
    mm::Layouts layouts;
    mm::GridLayout grid;
    grid.width = 16;
    grid.height = 16;
    grid.depth = 1;
    layouts.addChild(&grid);

    mm::Layer layer;
    layer.setLayouts(&layouts);
    layer.setChannelsPerLight(3);

    mm::ParticlesEffect particles;
    layer.addChild(&particles);

    layer.applyState();
    // The trail plane (w*h*cpl) PLUS the shared particle pool. The pool moved from a fixed member array into ScratchBuffers when this effect converged onto the kernel, so it is reported here where it used to be invisible: 64 particles x (4 int32 + uint16 + uint8) = 1216 bytes.
    constexpr size_t kTrail = 16 * 16 * 3;
    constexpr size_t kPool  = 64 * (4 * sizeof(int32_t) + sizeof(uint16_t) + sizeof(uint8_t));
    CHECK(particles.dynamicBytes() == kTrail + kPool);
}

// A single tick is enough to paint particles into the buffer.
TEST_CASE("ParticlesEffect renders non-zero buffer after one frame") {
    mm::Layouts layouts;
    mm::GridLayout grid;
    grid.width = 16;
    grid.height = 16;
    grid.depth = 1;
    layouts.addChild(&grid);

    mm::Layer layer;
    layer.setLayouts(&layouts);
    layer.setChannelsPerLight(3);

    mm::ParticlesEffect particles;
    layer.addChild(&particles);

    layer.applyState();
    layer.tick();

    auto& buf = layer.buffer();
    bool hasNonZero = false;
    for (size_t i = 0; i < buf.bytes(); i++) {
        if (buf.data()[i] != 0) { hasNonZero = true; break; }
    }
    CHECK(hasNonZero);
}

// Disabling the effect releases the trail buffer (dynamicBytes returns to 0).
TEST_CASE("ParticlesEffect frees trail buffer when disabled") {
    mm::Layouts layouts;
    mm::GridLayout grid;
    grid.width = 8;
    grid.height = 8;
    grid.depth = 1;
    layouts.addChild(&grid);

    mm::Layer layer;
    layer.setLayouts(&layouts);
    layer.setChannelsPerLight(3);

    mm::ParticlesEffect particles;
    layer.addChild(&particles);

    layer.applyState();
    CHECK(particles.dynamicBytes() > 0);

    particles.setEnabled(false);
    particles.applyState();
    CHECK(particles.dynamicBytes() == 0);
}
