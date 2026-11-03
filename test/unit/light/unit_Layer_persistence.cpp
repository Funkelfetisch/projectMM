/// Layer buffer persistence + the collected per-frame fade (fadeToBlackBy).
///
/// The Layer does NOT clear its buffer each frame (FastLED/WLED/MoonLight model): the buffer holds the previous frame so effects can fade it for trails or read prior pixels. Trail effects call layer->fadeToBlackBy(amt); the Layer collects the amount (MIN across effects) and applies ONE fade pass at the start of the next frame. These cases pin: (1) persistence, a pixel written one frame is still there the next; (2) fadeToBlackBy decays the persisted buffer once per frame; (3) MIN combine when several effects request a fade; (4) the collected amount resets after it is consumed.

#include "doctest.h"
#include "core/module/StateDocument.h"
#include "core/util/ModuleFactory.h"
#include <algorithm>
#include <cstring>
#include <string>
#include "light/layers/BlendMap.h"
#include "light/layers/Effects.h"
#include "light/layers/Layer.h"
#include "light/layouts/Layouts.h"
#include "light/layouts/GridLayout.h"
#include "light/layouts/RingLayout.h"
#include "light/effects/EffectBase.h"
#include "light/effects/ColorTrailsEffect.h"
#include "light/effects/ParticlesEffect.h"
#include "light/powerfunctions/draw.h"
#include "platform/platform.h"   // setTestNowMs, to drive the fade clock
#include "light/moonlive/MoonLiveEffect.h"
#include "core/moonlive/moonlive_emit.h"   // MM_MOONLIVE_HAS_HOST_JIT
#include "../core/moonlive_script_wrap.h"
#include "MoonLiveScriptFixture.h"

namespace {

// A test effect that writes one red pixel at (0,0) only on its FIRST loop, then does nothing, so any red still present on later frames proves the buffer persisted (was not cleared).
struct WriteOnceEffect : mm::EffectBase {
    int calls = 0;
    const char* tags() const override { return ""; }
    mm::Dim dimensions() const override { return mm::Dim::D3; }
    void tick() MM_NONBLOCKING override {
        if (calls++ == 0) {
            mm::Buffer& b = layer()->buffer();
            mm::Coord3D dims{width(), height(), depth()};
            mm::draw::pixel(b, dims, {0, 0, 0}, {255, 0, 0});
        }
    }
};

// A test effect that requests a fade of `amt` every frame and never writes, so it only decays whatever the buffer already holds.
struct FadeOnlyEffect : mm::EffectBase {
    uint8_t amt = 0;
    const char* tags() const override { return ""; }
    mm::Dim dimensions() const override { return mm::Dim::D3; }
    void tick() MM_NONBLOCKING override { layer()->fadeToBlackBy(amt); }
};

struct Scene {
    mm::Layouts layouts;
    mm::GridLayout grid;
    mm::Layer layer;
    Scene(int w, int h) {
        grid.width = w; grid.height = h; grid.depth = 1;
        layouts.addChild(&grid);
        layer.setLayouts(&layouts);
        layer.setChannelsPerLight(3);
    }
};

}  // namespace

TEST_CASE("a strip-native effect on a sparse ring uses one buffer cell per LED") {
    struct StripEffect : mm::EffectBase {
        bool usesPhysicalLightOrder() const override { return true; }
        void tick() MM_NONBLOCKING override { buffer()[3] = 91; }
    } effect;
    mm::RingLayout ring;
    ring.nrOfLEDs = 311;
    ring.radius = 24;
    mm::Layouts layouts;
    layouts.addChild(&ring);
    mm::Layer layer;
    layer.setLayouts(&layouts);
    layer.setChannelsPerLight(4);
    layer.addChild(&effect);
    layer.applyState();
    CHECK(layer.buffer().count() == 311);
    CHECK(layer.width() == 311);
    CHECK(layer.height() == 1);
    CHECK_FALSE(layer.lut().hasLUT());
    layer.tick();
    CHECK(layer.buffer().data()[3] == 91);
}

TEST_CASE("Layer: buffer persists across frames (no per-frame clear)") {
    Scene s(4, 4);
    WriteOnceEffect once;
    s.layer.addChild(&once);
    s.layer.applyState();

    s.layer.tick();                              // frame 0: writes red at (0,0)
    CHECK(s.layer.buffer().data()[0] == 255);
    s.layer.tick();                              // frame 1: writes nothing
    // The pixel is STILL there, the Layer did not wipe it. (An auto-clear would show 0 here.)
    CHECK(s.layer.buffer().data()[0] == 255);
    s.layer.tick();                              // frame 2
    CHECK(s.layer.buffer().data()[0] == 255);
}

// A trail decays with TIME, not with frames. Ticking repeatedly inside the same millisecond therefore fades almost nothing: the frames are real (a fast device draws the motion more smoothly) but no time has passed for the decay to spend. This is the property that makes a tail the same length on a 470 fps board and a 140,000 fps desktop, and it is the whole reason the Layer scales the requested rate rather than applying it once per frame.
TEST_CASE("a trail decays with elapsed time, not with the frame count") {
    Scene s(4, 4);
    WriteOnceEffect once;
    FadeOnlyEffect fade; fade.amt = 128;
    s.layer.addChild(&once);
    s.layer.addChild(&fade);
    s.layer.applyState();

    s.layer.tick();                              // frame 0: once writes 255
    REQUIRE(s.layer.buffer().data()[0] == 255);
    for (int i = 0; i < 200; i++) s.layer.tick();
    // 200 frames inside a few milliseconds: a per-frame fade would have wiped this to black many times over. Scaled by elapsed time, the pixel is still lit.
    CHECK(s.layer.buffer().data()[0] > 0);
}

TEST_CASE("Layer: multiple fade requests combine with MIN (gentlest wins, longest trail)") {
    Scene s(4, 4);
    WriteOnceEffect once;
    FadeOnlyEffect gentle; gentle.amt = 8;        // long trail
    FadeOnlyEffect harsh;  harsh.amt = 200;       // short trail
    s.layer.addChild(&once);
    s.layer.addChild(&gentle);
    s.layer.addChild(&harsh);
    s.layer.applyState();

    s.layer.tick();                              // frame 0: 255 written; both fades collected → MIN = 8
    s.layer.tick();                              // frame 1: consume MIN(8,200)=8 → keep = 247/255 of 255
    const uint8_t v = s.layer.buffer().data()[0];
    // With the gentle amount (8) the pixel stays near full; the harsh 200 would have crushed it to ~55.
    CHECK(v > 230);                              // proves MIN (gentle) won, not MAX/AVG
}

TEST_CASE("Layer: collected fade resets after it is consumed") {
    Scene s(4, 4);
    WriteOnceEffect once;
    // A fade effect that requests a fade only on the FIRST frame, then stops.
    struct OnceFade : mm::EffectBase {
        int n = 0;
        const char* tags() const override { return ""; }
        mm::Dim dimensions() const override { return mm::Dim::D3; }
        void tick() MM_NONBLOCKING override { if (n++ == 0) layer()->fadeToBlackBy(128); }
    } oneFade;
    s.layer.addChild(&once);
    s.layer.addChild(&oneFade);
    s.layer.applyState();

    s.layer.tick();                              // frame 0: 255 written, fade(128) collected
    s.layer.tick();                              // frame 1: consume once → ~127, no new fade requested
    const uint8_t after1 = s.layer.buffer().data()[0];
    s.layer.tick();                              // frame 2: NO fade pending → value must hold, not decay again
    CHECK(s.layer.buffer().data()[0] == after1); // stable: the collected amount did not linger
}

TEST_CASE("Layer: prepare clears the buffer (a rebuild wipes stale pixels)") {
    Scene s(4, 4);
    WriteOnceEffect once;
    s.layer.addChild(&once);
    s.layer.applyState();

    s.layer.tick();                              // writes red at (0,0)
    CHECK(s.layer.buffer().data()[0] == 255);

    // A rebuild (config change / resize) clears the buffer, persistence holds between frames but NOT across a rebuild, so a stale lit pixel must not survive it (else a reconfigure leaves ghosts).
    s.layer.applyState();
    const mm::Buffer& b = s.layer.buffer();
    bool allBlack = true;
    for (size_t i = 0; i < b.bytes(); i++)
        if (b.data()[i] != 0) { allBlack = false; break; }
    CHECK(allBlack);
}

TEST_CASE("Layer changes scenes without stale pixels or a black transition tick") {
    Scene s(4, 4);
    WriteOnceEffect red;
    struct BlueOnceEffect : mm::EffectBase {
        const char* tags() const override { return ""; }
        mm::Dim dimensions() const override { return mm::Dim::D3; }
        void tick() MM_NONBLOCKING override {
            mm::draw::pixel(layer()->buffer(), {width(), height(), depth()}, {1, 0, 0}, {0, 0, 255});
        }
    } blue;
    blue.setEnabled(false);
    s.layer.addChild(&red);
    s.layer.addChild(&blue);
    s.layer.applyState();

    s.layer.tick();
    REQUIRE(s.layer.buffer().data()[0] == 255);
    red.setEnabled(false);
    blue.setEnabled(true);
    s.layer.tick();
    CHECK(s.layer.buffer().data()[0] == 0);
    CHECK(s.layer.buffer().data()[5] == 255);

    blue.setEnabled(false);
    s.layer.tick();
    for (size_t i = 0; i < s.layer.buffer().bytes(); i++)
        CHECK(s.layer.buffer().data()[i] == 0);
}


#if MM_MOONLIVE_HAS_HOST_JIT
// A SCRIPT asking for the same fade a compiled effect asks for. This is the end-to-end seam: fade(amt) in the script text reaches the layer's collected fade through the binding, so a scripted effect gets trails on exactly the terms a C++ effect does. Needs a JIT backend, since nothing runs without one.
TEST_CASE("a scripted effect fades its layer the way a compiled one does") {
    Scene s(4, 4);
    WriteOnceEffect once;
    mm::MoonLiveEffect scripted;
    s.layer.addChild(&once);
    s.layer.addChild(&scripted);
    scripted.defineControls();
    scripted.setScript(mmWriteScript(mmScript("fade(128);")));
    s.layer.applyState();

    // The fade is a RATE the Layer scales by elapsed time, so the clock has to move for any of it to land. Two 16 ms frames is roughly two reference frames at the requested amount.
    mm::platform::setTestNowMs(100000u);
    s.layer.tick();                              // frame 0: red written, nothing faded yet
    REQUIRE(s.layer.buffer().data()[0] == 255);
    mm::platform::setTestNowMs(100016u);
    s.layer.tick();                              // collects the request
    mm::platform::setTestNowMs(100032u);
    s.layer.tick();                              // consumes it at the start of this frame
    const uint8_t after = s.layer.buffer().data()[0];
    mm::platform::setTestNowMs(0);
    CHECK(after < 255);                          // the script really reached the layer
    CHECK(after > 0);                            // and asked for a half fade, not a wipe
}

// What a trail actually IS, and the trap behind it: the buffer persists, so a fade applied EVERY frame decays a pixel by the frame rate, not by the motion. A desktop renders thousands of frames while a slow dot sits in one pixel, so a per-frame fade erases the tail long before the dot moves and the effect reads as "no trail" though every part works. A script that fades only when its subject MOVES gets the same trail on any renderer.
TEST_CASE("a trail survives many frames when the script fades only as it moves") {
    Scene s(4, 4);
    WriteOnceEffect once;
    mm::MoonLiveEffect scripted;
    s.layer.addChild(&once);
    s.layer.addChild(&scripted);
    scripted.defineControls();
    // Fades hard, but only on the first of every four frames.
    scripted.setScript(mmWriteScript(mmScript(
        "if (mod(tick, 4) == 0) { fade(120); }")));
    s.layer.applyState();

    s.layer.tick();                              // frame 0: red written
    REQUIRE(s.layer.buffer().data()[0] == 255);
    for (int i = 0; i < 3; i++) s.layer.tick();  // three frames with no fade requested
    const uint8_t held = s.layer.buffer().data()[0];
    CHECK(held == 255);                          // the tail is NOT eaten by the frame rate
}
#endif

namespace {
struct LowHeap {
    LowHeap() {
        mm::platform::setTestFreeHeap(mm::platform::HEAP_RESERVE - 1);
        mm::platform::setTestFreeInternalHeap(mm::platform::HEAP_RESERVE - 1);
        mm::platform::setTestMaxAllocBlock(1024 * 1024);
    }
    ~LowHeap() {
        mm::platform::setTestHeapCapacity(0);
        mm::platform::setTestFreeHeap(0);
        mm::platform::setTestFreeInternalHeap(0);
        mm::platform::setTestMaxAllocBlock(0);
    }
};
struct MemoryStripEffect : mm::EffectBase {
    bool physical = true;
    bool usesPhysicalLightOrder() const override { return physical; }
    mm::Dim dimensions() const override { return mm::Dim::D2; }
    void tick() MM_NONBLOCKING override {}
};
}

TEST_CASE("output memory: re-preparing an unchanged strip under pressure retains every LED") {
    mm::RingLayout ring;
    ring.nrOfLEDs = 222;
    ring.radius = 24;
    mm::Layouts layouts;
    layouts.addChild(&ring);
    mm::Layer layer;
    layer.setLayouts(&layouts);
    layer.setChannelsPerLight(4);
    MemoryStripEffect effect;
    layer.addChild(&effect);
    layer.applyState();
    REQUIRE(layer.buffer().count() == 222);
    LowHeap pressure;
    layer.applyState();
    CHECK(layer.buffer().count() == 222);
    CHECK(layer.width() == 222);
}

TEST_CASE("output memory: shrinking an existing raster does not lose a strip to the reserve") {
    Scene scene(444, 1);
    scene.layer.applyState();
    REQUIRE(scene.layer.buffer().count() == 444);
    scene.grid.width = 222;
    scene.grid.applyState();
    LowHeap pressure;
    scene.layer.applyState();
    CHECK(scene.layer.buffer().count() == 222);
}

TEST_CASE("output memory: reduced resolution still reaches every physical LED") {
    for (bool physical : {false, true}) {
        mm::RingLayout ring;
        ring.nrOfLEDs = 16;
        ring.radius = 8;
        mm::Layouts layouts;
        layouts.addChild(&ring);
        mm::Layer layer;
        layer.setLayouts(&layouts);
        MemoryStripEffect effect;
        effect.physical = physical;
        layer.addChild(&effect);
        LowHeap pressure;
        // Enough for a reduced geometric table and buffer, with real cumulative debit; a fresh mapping cannot consume the shared reserve itself.
        mm::platform::setTestHeapCapacity(mm::platform::allocatedBytes() + mm::platform::HEAP_RESERVE + 512);
        layer.applyState();
        REQUIRE(layer.buffer().data() != nullptr);
        CHECK(mm::platform::freeHeap() >= mm::platform::HEAP_RESERVE);
        if (!physical) CHECK(layer.width() < layer.physicalWidth());
        std::fill(layer.buffer().span().begin(), layer.buffer().span().end(), uint8_t{91});
        mm::Buffer output;
        REQUIRE(output.allocate(16, 3));
        mm::blendMap(layer.buffer(), output, layer.lut(), 3);
        CHECK(std::all_of(output.span().begin(), output.span().end(), [](uint8_t value) { return value == 91; }));
    }
}

TEST_CASE("working memory: effect scratch fits the reserve without losing physical LEDs") {
    struct Restore {
        ~Restore() {
            mm::platform::setTestFreeHeap(0); mm::platform::setTestFreeInternalHeap(0);
            mm::platform::setTestMaxAllocBlock(0);
        }
    } restore;
    for (bool particles : {false, true}) {
        mm::RingLayout ring; ring.nrOfLEDs = 222; ring.radius = 24;
        mm::Layouts layouts; layouts.addChild(&ring);
        mm::Layer layer; layer.setLayouts(&layouts); layer.setChannelsPerLight(4);
        mm::ColorTrailsEffect trails; mm::ParticlesEffect swarm;
        mm::EffectBase& effect = particles ? static_cast<mm::EffectBase&>(swarm)
                                           : static_cast<mm::EffectBase&>(trails);
        layer.addChild(&effect);
        mm::platform::setTestFreeHeap(52000); mm::platform::setTestFreeInternalHeap(52000);
        mm::platform::setTestMaxAllocBlock(1024 * 1024);
        layer.applyState();
        CHECK(layer.width() < layer.physicalWidth());
        CHECK(layer.severity() == mm::MoonModule::Severity::Warning);
        CHECK(layer.dynamicBytes() + effect.dynamicBytes() <= 52000 - mm::platform::HEAP_RESERVE);
        std::fill(layer.buffer().span().begin(), layer.buffer().span().end(), uint8_t{91});
        mm::Buffer output; REQUIRE(output.allocate(222, 4));
        mm::blendMap(layer.buffer(), output, layer.lut(), 4);
        CHECK(std::all_of(output.span().begin(), output.span().end(), [](uint8_t value) { return value == 91; }));
        const auto count = layer.buffer().count();
        mm::platform::setTestFreeHeap(mm::platform::HEAP_RESERVE - 1);
        layer.applyState();
        CHECK(layer.buffer().count() == count);
        CHECK(effect.severity() != mm::MoonModule::Severity::Error);
    }
}

TEST_CASE("working memory: an empty layer holds no effect scratch") {
    mm::Layer layer; mm::ColorTrailsEffect trails; mm::ParticlesEffect swarm;
    layer.addChild(&trails); layer.addChild(&swarm); layer.applyState();
    CHECK(trails.dynamicBytes() == 0);
    CHECK(swarm.dynamicBytes() == 0);
    CHECK(trails.status() == nullptr);
    CHECK(swarm.status() == nullptr);
}

TEST_CASE("working memory: product control headroom reduces scratch rather than physical coverage") {
    struct Restore {
        ~Restore() {
            mm::platform::setTestFreeHeap(0); mm::platform::setTestFreeInternalHeap(0);
            mm::platform::setTestMaxAllocBlock(0);
        }
    } restore;
    for (size_t reserve : {mm::platform::HEAP_RESERVE, size_t{40 * 1024}}) {
        mm::RingLayout ring; ring.nrOfLEDs = 222; ring.radius = 24;
        mm::Layouts layouts; layouts.addChild(&ring);
        mm::Layer layer; layer.setLayouts(&layouts); layer.setChannelsPerLight(4);
        layer.setWorkingMemoryReserve(reserve);
        mm::ParticlesEffect swarm; layer.addChild(&swarm);
        mm::platform::setTestFreeHeap(60000); mm::platform::setTestFreeInternalHeap(60000);
        mm::platform::setTestMaxAllocBlock(1024 * 1024);
        layer.applyState();
        if (reserve == mm::platform::HEAP_RESERVE) {
            CHECK(layer.width() == layer.physicalWidth());
        } else {
            CHECK(layer.width() < layer.physicalWidth());
            CHECK(std::strstr(layer.status(), "memory reduced") != nullptr);
            const std::string dimensions = std::to_string(layer.width()) + "×" + std::to_string(layer.height()) + "×" + std::to_string(layer.depth());
            CHECK(std::string(layer.status()).find(dimensions) == 0);
        }
        // The injected free-heap value is fixed through LUT construction: it models the admission-time budget.
        CHECK(layer.buffer().bytes() + swarm.dynamicBytes() <= 60000 - reserve);
        std::fill(layer.buffer().span().begin(), layer.buffer().span().end(), uint8_t{91});
        mm::Buffer output; REQUIRE(output.allocate(222, 4));
        mm::blendMap(layer.buffer(), output, layer.lut(), 4);
        CHECK(std::all_of(output.span().begin(), output.span().end(), [](uint8_t value) { return value == 91; }));
    }
}

TEST_CASE("working memory: API-created scratch is admitted before the next rendered frame") {
    struct Restore {
        ~Restore() {
            mm::platform::setTestHeapCapacity(0);
            mm::platform::setTestFreeHeap(0); mm::platform::setTestFreeInternalHeap(0);
            mm::platform::setTestMaxAllocBlock(0);
        }
    } restore;
    REQUIRE(mm::ModuleFactory::registerType<mm::ParticlesEffect>("MemoryBudgetParticlesEffect"));
    mm::Scheduler scheduler;
    mm::RingLayout ring; ring.nrOfLEDs = 222; ring.radius = 24;
    mm::Layouts layouts; layouts.setName("Layouts"); layouts.addChild(&ring);
    mm::Effects effects; effects.setName("Effects"); effects.setLayouts(&layouts);
    mm::Layer layer; layer.setName("Layer"); layer.setChannelsPerLight(4);
    layer.setWorkingMemoryReserve(40 * 1024); effects.addChild(&layer);
    struct Cleanup {
        mm::Layer& layer;
        ~Cleanup() {
            while (layer.childCount()) {
                auto* effect = layer.child(0); layer.removeChild(effect);
                mm::Scheduler::deleteTree(effect);
            }
        }
    } cleanup{layer};
    scheduler.addModule(&layouts); scheduler.addModule(&effects); scheduler.setup();
    REQUIRE(layer.width() == layer.physicalWidth());
    const auto result = mm::applyStateDocument(scheduler,
        R"({"Effects":{"Layer":{"MemoryScene":{"type":"MemoryBudgetParticlesEffect","enabled":true}}}})");
    REQUIRE(result.ok);
    REQUIRE(layer.childCount() == 1);
    auto* effect = layer.child(0);
    const size_t initialScratch = effect->dynamicBytes();
    REQUIRE(initialScratch > 10000);   // the API initialized it against the previous full raster
    mm::platform::setTestHeapCapacity(mm::platform::allocatedBytes() + 32000);
    mm::platform::setTestMaxAllocBlock(1024 * 1024);
    scheduler.preparePending();         // the same frame-boundary rebuild as the real API
    CHECK(layer.width() < layer.physicalWidth());
    CHECK(effect->dynamicBytes() < initialScratch);
    std::fill(layer.buffer().span().begin(), layer.buffer().span().end(), uint8_t{91});
    mm::Buffer output; REQUIRE(output.allocate(222, 4));
    mm::blendMap(layer.buffer(), output, layer.lut(), 4);
    CHECK(std::all_of(output.span().begin(), output.span().end(), [](uint8_t value) { return value == 91; }));
    const auto admittedCount = layer.buffer().count();
    scheduler.prepareTree();
    CHECK(layer.buffer().count() == admittedCount);   // an unchanged scene retains the admitted allocation
}
