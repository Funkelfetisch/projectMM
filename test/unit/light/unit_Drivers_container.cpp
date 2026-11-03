/// @module Drivers

#include "doctest.h"
#include "core/module/Scheduler.h"
#include "core/module/StateDocument.h"
#include "light/drivers/Drivers.h"
#include "light/layers/Layer.h"
#include "light/layouts/GridLayout.h"
#include "light/layouts/Layouts.h"
#include "light/drivers/FixtureProfilesModule.h"   // the non-deletable boot-wired fixture-profile library
#include "light/drivers/NetworkSendDriver.h"     // a real driver, for the sibling-instance cases
#include "light/drivers/ParallelLedDriver.h"     // its installer and bench controls, for the mode case
#include "light/drivers/RmtLedDriver.h"
#include "light/drivers/PreviewDriver.h"     // the one driver safe mode keeps
#include "light/drivers/PwmLightDriver.h"
#include "light/effects/SolidEffect.h"
#include "light/layers/Effects.h"
#include "correction_presets.h"                  // mm::test::rebuildFromPreset
#include "../core/conditional_controls.h"   // mm::test::setControlValue
#include "platform/platform.h"                 // gpioRead: the desktop reads back what gpioWrite put there

#include <cstring>
#include <filesystem>
#include <string>

// Drivers::tick() honors the per-child `enabled` check; a stub driver counts its loop calls, and toggling `enabled` must flip whether the count advances.

namespace {

// Minimal DriverBase stub: counts loop calls. Ignores the source buffer it would normally consume, this test cares only about whether tick() runs.
class CountingDriver : public mm::DriverBase {
public:
    void setSourceBuffer(mm::Buffer*) override {}
    void tick() MM_NONBLOCKING override { loopCalls++; }
    int loopCalls = 0;
};

class PowerPinOwner : public CountingDriver {
public:
    bool reserved = true;
    bool reservesPowerPin(uint8_t pin) const override { return reserved && pin == 17; }
};

class RgbFrameDriver : public mm::DriverBase {
public:
    void setSourceBuffer(mm::Buffer* buffer) override { source = buffer; }
    void defineDriverControls() override { defineCorrectionControls(); }
    void tick() MM_NONBLOCKING override {
        if (source && source->data()) correction_.apply(source->data(), output, source->channelsPerLight());
    }
    mm::Buffer* source = nullptr;
    uint8_t output[3] = {};
};

struct ManualWhiteRig {
    mm::FixtureProfilesModule profiles;
    mm::Layouts layouts;
    mm::GridLayout grid;
    mm::Effects effects;
    mm::Layer layer;
    mm::SolidEffect solid;
    mm::Drivers drivers;
    RgbFrameDriver rgb;
    mm::PwmLightDriver accent;

    explicit ManualWhiteRig(bool withWhite = true) {
        profiles.setup();
        grid.width = 2; grid.height = 1; grid.depth = 1;
        layouts.addChild(&grid);
        layouts.applyState();
        effects.addChild(&layer);
        effects.setLayouts(&layouts);
        layer.addChild(&solid);
        drivers.multicore = false;
        drivers.brightness = 255;
        drivers.on = true;
        drivers.setEffects(&effects);
        rgb.setDefaultCorrection("RGB", mm::WhiteMode::None);
        rgb.setDefaultCurve(mm::Correction::Curve::Linear);
        accent.setDefaultCorrection("White", mm::WhiteMode::None);
        accent.setDefaultCurve(mm::Correction::Curve::Linear);
        std::strcpy(accent.pins, "46");
        accent.bits = 12;
        accent.setWindow(1, 1);
        drivers.addChild(&rgb);
        if (withWhite) drivers.addChild(&accent);
    }
    void prepare() {
        drivers.defineControls();
        drivers.setup();
        layer.prepare();
        drivers.prepare();
        if (accent.parent() == &drivers && accent.enabled()) accent.prepare();
    }
    ~ManualWhiteRig() {
        drivers.release();
        drivers.removeChild(&accent);
        drivers.removeChild(&rgb);
        layer.removeChild(&solid);
        effects.removeChild(&layer);
        layouts.removeChild(&grid);
    }
};

mm::Drivers* mutationDrivers = nullptr;

struct DriverMutationHook {
    explicit DriverMutationHook(mm::Drivers& drivers) {
        mutationDrivers = &drivers;
        mm::MoonModule::setQuiesceRenderHook([] {
            if (mutationDrivers) mutationDrivers->quiesceRenderSplit();
        });
    }
    ~DriverMutationHook() {
        mm::MoonModule::setQuiesceRenderHook(nullptr);
        mutationDrivers = nullptr;
    }
};

struct DeletedLayer : mm::Layer {
    explicit DeletedLayer(bool& deleted) : deleted_(deleted) {}
    ~DeletedLayer() override { deleted_ = true; }
    bool& deleted_;
};

// The drivers borrow a heap-owned Layer's Buffer in the single-core identity path, so StateDocument must sever those borrows before it destroys that Layer.
struct DriverLifetimeRig {
    mm::FixtureProfilesModule profiles;
    mm::Layouts layouts;
    mm::GridLayout grid;
    mm::Effects effects;
    mm::Drivers drivers;
    mm::NetworkSendDriver network;
    mm::RmtLedDriver rmt;
    mm::Scheduler scheduler;

    explicit DriverLifetimeRig(bool& deleted) {
        profiles.setName("Fixtures"); profiles.setTypeName("FixtureProfiles");
        layouts.setName("Layouts"); layouts.setTypeName("Layouts");
        effects.setName("Effects"); effects.setTypeName("Effects");
        drivers.setName("Drivers"); drivers.setTypeName("Drivers");
        grid.width = 2; grid.height = 1; grid.depth = 1;
        layouts.addChild(&grid);
        effects.setLayouts(&layouts);
        auto* old = new DeletedLayer(deleted);
        old->setName("Old Layer"); old->setTypeName("Layer");
        effects.addChild(old);
        drivers.multicore = false;
        drivers.brightness = 255;
        drivers.on = true;
        drivers.setEffects(&effects);
        network.setName("Network"); network.setTypeName("NetworkSend");
        rmt.setName("RMT"); rmt.setTypeName("RmtLed");
        network.setDefaultCorrection("RGB", mm::WhiteMode::None);
        rmt.setDefaultCorrection("RGB", mm::WhiteMode::None);
        std::strcpy(rmt.pins, "35");
        std::strcpy(rmt.ledsPerPin, "2");
        // Empty hosts keep the real Network driver local to the fixture.
        network.hosts[0] = '\0';
        drivers.addChild(&network);
        drivers.addChild(&rmt);
        scheduler.addModule(&profiles);
        scheduler.addModule(&layouts);
        scheduler.addModule(&effects);
        scheduler.addModule(&drivers);
        scheduler.setup();
    }
    ~DriverLifetimeRig() {
        drivers.setEffects(nullptr);
        drivers.release();
        drivers.removeChild(&network);
        drivers.removeChild(&rmt);
        while (effects.childCount()) {
            auto* child = effects.child(0);
            effects.removeChild(child);
            mm::Scheduler::deleteTree(child);
        }
        layouts.removeChild(&grid);
    }
};

} // namespace

TEST_CASE("Drivers detach real Network and RMT buffers before StateDocument deletes their heap layer") {
    bool deleted = false;
    DriverLifetimeRig rig(deleted);
    DriverMutationHook hook(rig.drivers);
    auto* old = rig.effects.activeLayer();
    REQUIRE(old != nullptr);
    REQUIRE(old->buffer().data() != nullptr);
    REQUIRE(old->buffer().count() == 2);
    REQUIRE(rig.network.layer() == old);
    REQUIRE(rig.rmt.layer() == old);
    const auto prepared = rig.scheduler.preparationRevision();

    const auto removed = mm::applyStateDocument(rig.scheduler, R"({"Effects":{"Old Layer":null}})");
    INFO(removed.error);
    REQUIRE(removed.ok);
    REQUIRE(deleted); // the former Buffer owner is gone, rather than merely detached alive
    REQUIRE(rig.effects.childCount() == 0);
    CHECK(rig.scheduler.preparationRevision() == prepared);
    CHECK(rig.network.layer() == nullptr);
    CHECK(rig.rmt.layer() == nullptr);

    // Both real correction callbacks run in the delete/prepare gap: Network resizes unconditionally, and RGB->RGBW forces RMT to re-parse the source geometry.
    REQUIRE(rig.scheduler.setControl("Network", "fixture", R"({"value":"RGBW"})") == mm::Scheduler::SetControlResult::Ok);
    REQUIRE(rig.scheduler.setControl("RMT", "fixture", R"({"value":"RGBW"})") == mm::Scheduler::SetControlResult::Ok);
    rig.drivers.fixtureChannels();
    CHECK(rig.scheduler.preparationRevision() == prepared);
    CHECK(rig.network.layer() == nullptr);
    CHECK(rig.rmt.layer() == nullptr);

    auto* fresh = new mm::Layer;
    fresh->setName("New Layer"); fresh->setTypeName("Layer");
    REQUIRE(rig.effects.addChild(fresh));
    rig.scheduler.preparePending();
    REQUIRE(fresh->buffer().data() != nullptr);
    CHECK(fresh->buffer().count() == 2);
    CHECK(fresh->buffer().channelsPerLight() == 4);
    CHECK(rig.network.layer() == fresh);
    CHECK(rig.rmt.layer() == fresh);
    // Rebinding restores the real correction callbacks after the empty interval too.
    rig.network.rebuildCorrection(128);
    rig.rmt.rebuildCorrection(128);
}

TEST_CASE("Drivers manual white capability widens a default layer and carries Solid white to native PWM") {
    ManualWhiteRig rig;
    REQUIRE(rig.layer.channelsPerLight() == 3);
    rig.prepare();
    REQUIRE(rig.layer.channelsPerLight() == 4);
    REQUIRE(rig.layer.buffer().channelsPerLight() == 4);
    CHECK(rig.layer.fixtureChannels().colorChannels == 4);
    rig.solid.red = 200;
    rig.solid.green = 100;
    rig.solid.blue = 50;
    rig.solid.white = 128;
    rig.solid.brightness = 255;
    rig.layer.tick();
    REQUIRE(rig.layer.buffer().data() != nullptr);
    CHECK(rig.layer.buffer().data()[3] == 128);
    rig.drivers.tick();
    CHECK(rig.rgb.output[0] == 200);
    CHECK(rig.rgb.output[1] == 100);
    CHECK(rig.rgb.output[2] == 50);
    REQUIRE(rig.accent.channelForTest(0) >= 0);
    CHECK(mm::platform::pwmPinForTest(rig.accent.channelForTest(0)) == 46);
    CHECK(mm::platform::pwmDutyForTest(rig.accent.channelForTest(0)) == (128u * 4095u + 127u) / 255u);
}

TEST_CASE("Drivers manual white capability is absent for RGB only disabled or removed white outputs") {
    bool withWhite = true;
    bool disabled = false;
    bool removed = false;
    SUBCASE("RGB only") { withWhite = false; }
    SUBCASE("disabled white") { disabled = true; }
    SUBCASE("removed white") { removed = true; }
    ManualWhiteRig rig(withWhite);
    if (disabled) rig.accent.setEnabled(false);
    if (removed) REQUIRE(rig.drivers.removeChild(&rig.accent));
    rig.prepare();
    CHECK(rig.drivers.fixtureChannels().colorChannels == 3);
    CHECK(rig.layer.channelsPerLight() == 3);
}

TEST_CASE("Drivers manual white capability survives layer replacement and never shrinks a wider layer") {
    ManualWhiteRig rig;
    rig.prepare();
    REQUIRE(rig.layer.channelsPerLight() == 4);
    mm::Layer replacement;
    REQUIRE(rig.effects.removeChild(&rig.layer));
    rig.effects.addChild(&replacement);
    replacement.setLayouts(&rig.layouts);
    CHECK(replacement.channelsPerLight() == 3);
    rig.drivers.prepare();
    replacement.prepare();
    CHECK(replacement.fixtureChannels().colorChannels == 4);
    CHECK(replacement.channelsPerLight() == 4);
    replacement.setChannelsPerLight(11);
    rig.drivers.prepare();
    replacement.prepare();
    CHECK(replacement.channelsPerLight() == 11);
    rig.accent.setEnabled(false);
    rig.drivers.prepare();
    replacement.prepare();
    CHECK(replacement.fixtureChannels().colorChannels == 3);
    CHECK(replacement.channelsPerLight() == 11);
    rig.effects.removeChild(&replacement);
    rig.effects.addChild(&rig.layer);
    rig.drivers.prepare();
    replacement.release();
}

TEST_CASE("Drivers manual white capability survives the production Scheduler setup and replacement prepare order") {
    ManualWhiteRig rig;
    mm::Scheduler scheduler;
    rig.layouts.setName("Layouts"); rig.layouts.setTypeName("Layouts");
    rig.effects.setName("Effects"); rig.effects.setTypeName("Effects");
    rig.drivers.setName("Drivers"); rig.drivers.setTypeName("Drivers");
    scheduler.addModule(&rig.layouts);
    scheduler.addModule(&rig.effects);
    scheduler.addModule(&rig.drivers);
    REQUIRE(rig.layer.channelsPerLight() == 3);
    scheduler.setup();
    REQUIRE(rig.drivers.fixtureChannels().colorChannels == 4);
    REQUIRE(rig.layer.channelsPerLight() == 4);
    REQUIRE(rig.layer.buffer().channelsPerLight() == 4);
    mm::Layer replacement;
    struct Restore {
        ManualWhiteRig& rig;
        mm::Layer& replacement;
        ~Restore() {
            rig.effects.removeChild(&replacement);
            if (rig.layer.parent() != &rig.effects) rig.effects.addChild(&rig.layer);
            rig.drivers.setEffects(&rig.effects);
            rig.drivers.prepare();
        }
    } restore{rig, replacement};
    REQUIRE(rig.effects.removeChild(&rig.layer));
    REQUIRE(rig.effects.addChild(&replacement));
    REQUIRE(replacement.channelsPerLight() == 3);
    scheduler.prepareTree();
    CHECK(replacement.fixtureChannels().colorChannels == 4);
    CHECK(replacement.channelsPerLight() == 4);
    CHECK(replacement.buffer().channelsPerLight() == 4);
}

TEST_CASE("Drivers manual white capability widens on a live fixture edit and avoids redundant prepares") {
    bool alreadyWide = false;
    SUBCASE("default RGB layer needs one prepare") {}
    SUBCASE("existing wider layer retains its width") { alreadyWide = true; }
    ManualWhiteRig rig;
    mm::Scheduler scheduler;
    rig.layouts.setName("Layouts"); rig.layouts.setTypeName("Layouts");
    rig.effects.setName("Effects"); rig.effects.setTypeName("Effects");
    rig.drivers.setName("Drivers"); rig.drivers.setTypeName("Drivers");
    rig.rgb.setName("RGB");
    rig.accent.setName("Accent");
    rig.accent.setDefaultCorrection("RGB", mm::WhiteMode::None);
    if (alreadyWide) rig.layer.setChannelsPerLight(11);
    scheduler.addModule(&rig.layouts);
    scheduler.addModule(&rig.effects);
    scheduler.addModule(&rig.drivers);
    scheduler.setup();
    REQUIRE(rig.layer.channelsPerLight() == (alreadyWide ? 11 : 3));
    const auto initial = scheduler.preparationRevision();
    REQUIRE(scheduler.setControl("Accent", "fixture", R"({"value":"White"})") == mm::Scheduler::SetControlResult::Ok);
    scheduler.preparePending();
    if (alreadyWide) CHECK(scheduler.preparationRevision() - initial <= 1);
    else CHECK(scheduler.preparationRevision() == initial + 1);
    REQUIRE(rig.layer.channelsPerLight() == (alreadyWide ? 11 : 4));
    rig.solid.red = 200; rig.solid.green = 100; rig.solid.blue = 50; rig.solid.white = 128;
    rig.layer.tick();
    rig.drivers.tick();
    CHECK(rig.rgb.output[0] == 200);
    CHECK(rig.rgb.output[1] == 100);
    CHECK(rig.rgb.output[2] == 50);
    REQUIRE(rig.accent.channelForTest(0) >= 0);
    CHECK(mm::platform::pwmDutyForTest(rig.accent.channelForTest(0)) == (128u * 4095u + 127u) / 255u);
    const auto prepared = scheduler.preparationRevision();
    REQUIRE(scheduler.setControl("Accent", "fixture", R"({"value":"White"})") == mm::Scheduler::SetControlResult::Ok);
    REQUIRE(scheduler.setControl("Drivers", "brightness", R"({"value":128})") == mm::Scheduler::SetControlResult::Ok);
    scheduler.notifyListChanged(rig.profiles);
    scheduler.preparePending();
    CHECK(scheduler.preparationRevision() == prepared);
}

// A minimal driver that owns its Correction (filled by the container via rebuildCorrection()) and defines the correction controls, so a test can read the resulting LUT.
class CorrectionCapturingDriver : public mm::DriverBase {
public:
    void setSourceBuffer(mm::Buffer*) override {}
    void tick() MM_NONBLOCKING override {}
    void defineDriverControls() override { defineCorrectionControls(); }
};

TEST_CASE("Drivers manual white capability is folded after the first motion output without replacing its map") {
    mm::FixtureProfilesModule profiles;
    profiles.setup();
    mm::Drivers drivers;
    CorrectionCapturingDriver motion, white;
    motion.setDefaultCorrection("MH BeeEyes 15", mm::WhiteMode::None);
    white.setDefaultCorrection("White", mm::WhiteMode::None);
    motion.defineControls();
    white.defineControls();
    drivers.addChild(&motion);
    const auto before = drivers.fixtureChannels();
    REQUIRE(before.movable());
    REQUIRE(before.colorChannels == 3);
    drivers.addChild(&white);
    const auto after = drivers.fixtureChannels();
    CHECK(after.colorChannels == 4);
    CHECK(after.pan == before.pan);
    CHECK(after.tilt == before.tilt);
    CHECK(after.zoom == before.zoom);
    CHECK(after.rotate == before.rotate);
    CHECK(after.gobo == before.gobo);
    white.setEnabled(false);
    CHECK(drivers.fixtureChannels().colorChannels == 3);
    drivers.removeChild(&white);
    drivers.removeChild(&motion);
}

// Counts prepare() (the structural rebuild that reinits the output peripheral) against onCorrectionChanged() (the light refresh), to prove a refresh is correction-only.
class RebuildTrackingDriver : public mm::DriverBase {
public:
    void setSourceBuffer(mm::Buffer*) override {}
    void tick() MM_NONBLOCKING override {}
    void defineDriverControls() override { defineCorrectionControls(); }
    void prepare() override { prepareCalls++; }
    void onCorrectionChanged() override { correctionCalls++; }
    int prepareCalls = 0;
    int correctionCalls = 0;
};

// Regression: a profile edit re-ran prepare on the whole tree, whose output reinit blanked every strip for half a second; the list-changed hook re-resolves the correction alone.
TEST_CASE("A fixture-profile list edit re-resolves each driver's correction without re-preparing it") {
    mm::FixtureProfilesModule lib;       // construction claims the library seat
    mm::Drivers drivers;
    RebuildTrackingDriver drv;
    drivers.addChild(&drv);
    drivers.on = true;
    drivers.brightness = 200;
    drv.defineControls();
    drivers.setup();                     // seeds each driver's correction
    const int baselineCorrection = drv.correctionCalls;
    const int baselinePrepare = drv.prepareCalls;

    drv.onListChanged(lib);
    CHECK(drv.correctionCalls > baselineCorrection);   // the edit reaches the output
    CHECK(drv.prepareCalls == baselinePrepare);        // with no peripheral reinit, so no blank

    // Another module's list is not one a driver resolves from.
    const int afterLib = drv.correctionCalls;
    drv.onListChanged(drivers);
    CHECK(drv.correctionCalls == afterLib);
}

// A driver that re-lays its frame on a correction change, as the parallel and RMT drivers do, counting the times it does.
class FrameLayingDriver : public mm::DriverBase {
public:
    void setSourceBuffer(mm::Buffer*) override {}
    void tick() MM_NONBLOCKING override {}
    void defineDriverControls() override { defineCorrectionControls(); }
    void onCorrectionChanged() override { if (outChannelsChanged()) layouts++; }
    void setOutChannels(uint8_t ch) { correction_.outChannels = ch; }
    int layouts = 0;
};

// Regression: every brightness change made the LightCrafter's parallel driver drain its DMA transfer and re-parse its lanes, which a desk fader's stream turned into freezes.
TEST_CASE("a brightness change re-bakes the correction without re-laying the driver's frame; a channel-count change does") {
    mm::Drivers drivers;
    FrameLayingDriver drv;
    drivers.addChild(&drv);
    drivers.on = true;
    drivers.brightness = 200;
    drv.defineControls();
    drivers.setup();
    const int afterSetup = drv.layouts;
    for (const uint8_t b : {10, 90, 255, 0, 61}) {
        drivers.brightness = b;
        drivers.onControlChanged("brightness");
    }
    CHECK(drv.layouts == afterSetup);
    drv.setOutChannels(static_cast<uint8_t>(drv.correction().outChannels + 1));   // as a white channel being added would
    drivers.onControlChanged("brightness");
    CHECK(drv.layouts == afterSetup + 1);
    drivers.removeChild(&drv);
}

// Core reads the light pipeline through LightOutput, so the seat must hold only a live, prepared Drivers.
TEST_CASE("Drivers is the light output core reads, from prepare until release") {
    mm::Drivers drivers;
    CHECK(mm::LightOutput::active() != &drivers);   // a probe that never prepares never publishes
    drivers.prepare();
    CHECK(mm::LightOutput::active() == &drivers);
    CHECK(&mm::lightSummary() == &drivers.summary());
    // A palette's own color finds a palette of the same color.
    const mm::LightOutput& out = drivers;
    const mm::RGB c = out.paletteRgb(out.nearestPalette(mm::RGB{255, 0, 0}));
    CHECK(c.r == 255);
    drivers.release();
    CHECK(mm::LightOutput::active() == nullptr);
    CHECK(mm::lightSummary().lightCount == 0);      // the all-zero summary, never a dangling one
}

// The `on` control is master power: on=false scales the correction LUT to zero while preserving the brightness value, so on=true restores the exact level, with no pipeline realloc.
TEST_CASE("Drivers::on gates the correction LUT without clobbering brightness") {
    mm::Drivers drivers;
    CorrectionCapturingDriver drv;
    drivers.addChild(&drv);
    drv.defineControls();
    // Linear keeps the expected numbers literal; a perceptual curve would turn each into a lookup.
    mm::test::setControlValue<uint8_t>(drv, "curve", static_cast<uint8_t>(mm::Correction::Curve::Linear));
    drivers.setup();                       // seeds drv's own correction_ from on(true)+brightness

    drivers.brightness = 200;
    drivers.on = true;
    drivers.onControlChanged("brightness");        // rebuild LUT at full power
    CHECK(drivers.effectiveBrightness() == 200);
    CHECK(drv.correctionForTest().briLut[255] == 200);   // (255 * 200) / 255 == 200

    // Turn off → LUT scales to black, but the brightness value is untouched.
    drivers.on = false;
    drivers.onControlChanged("on");
    CHECK(drivers.brightness == 200);            // value preserved
    CHECK(drivers.effectiveBrightness() == 0);
    CHECK(drv.correctionForTest().briLut[255] == 0);     // output black

    // Turn back on → the exact level returns, no stored-value juggling.
    drivers.on = true;
    drivers.onControlChanged("on");
    CHECK(drv.correctionForTest().briLut[255] == 200);
}

// A per-driver localBrightness change re-scales that driver's correction LUT (global × local), as a global brightness change does.
TEST_CASE("Drivers: a localBrightness change re-scales the driver's correction LUT") {
    mm::Drivers drivers;
    CorrectionCapturingDriver drv;
    drivers.addChild(&drv);
    drivers.brightness = 200;
    drivers.on = true;
    drv.defineControls();                           // bind the correction controls (localBrightness etc.)
    // LINEAR, so the arithmetic below reads as the multiplication it is testing. What this pins is that BOTH sliders reach the LUT, which a perceptual curve would leave true but express as table lookups nobody can check by eye. The curve has its own tests.
    mm::test::setControlValue<uint8_t>(drv, "curve", static_cast<uint8_t>(mm::Correction::Curve::Linear));
    drivers.setup();                                // seeds the driver's correction (global 200, local 255)
    CHECK(drv.correctionForTest().briLut[255] == 200);   // global 200 × local 255/255 = 200

    // Halve the driver's LOCAL brightness, its own control change must re-bake the LUT to global × local = 200 × 128/255 ≈ 100. This is the path the bug missed.
    mm::test::setControlValue<uint8_t>(drv, "localBrightness", 128);
    drv.onControlChanged("localBrightness");
    CHECK(drv.correctionForTest().briLut[255] == 100);   // (200 * 128) / 255 == 100

    // And the global slider still composes on top: raising global to 255 with local 128 → 128.
    drivers.brightness = 255;
    drivers.onControlChanged("brightness");
    CHECK(drv.correctionForTest().briLut[255] == 128);   // (255 * 128) / 255 == 128
}

// Disabled child drivers don't tick: toggling `enabled` flips whether that driver's tick() runs.
TEST_CASE("Drivers::tick() skips disabled child drivers") {
    mm::Drivers drivers;
    CountingDriver a, b;
    drivers.addChild(&a);
    drivers.addChild(&b);

    // Both enabled by default → both tick.
    drivers.tick();
    CHECK(a.loopCalls == 1);
    CHECK(b.loopCalls == 1);

    // Disable `a` → only `b` ticks.
    a.setEnabled(false);
    drivers.tick();
    CHECK(a.loopCalls == 1);  // unchanged
    CHECK(b.loopCalls == 2);

    // Disable `b` too → neither ticks.
    b.setEnabled(false);
    drivers.tick();
    CHECK(a.loopCalls == 1);
    CHECK(b.loopCalls == 2);

    // Re-enable `a` → only `a` ticks.
    a.setEnabled(true);
    drivers.tick();
    CHECK(a.loopCalls == 2);
    CHECK(b.loopCalls == 2);
}

// The "+ add" picker under Drivers offers only drivers, so acceptsChildRoles returns "driver" alone; the boot-wired FixtureProfiles library bypasses it.
TEST_CASE("Drivers accepts only driver-role children in the add picker") {
    mm::Drivers drivers;
    CHECK(std::strcmp(drivers.acceptsChildRoles(), "driver") == 0);
}

// The boot-wired fixture-profile library is a permanent singleton: a deleted one could never be re-added, and every driver resolves its profile through it.
TEST_CASE("FixtureProfiles library is a non-deletable singleton") {
    mm::FixtureProfilesModule lib;
    CHECK_FALSE(lib.userEditable());
}

// The scripted-palette list is device-wide storage, so a throwaway probe Drivers reads it without rescanning, and leaves nothing dangling when it goes.
TEST_CASE("a probe Drivers reads the scripted-palette list without rescanning it") {
    { mm::Drivers running; running.defineControls(); }   // the list scanned, as the running instance does at boot
    const uint8_t before = mm::LivePalettes::count();
    mm::platform::fsMkdir(mm::moonlive::kScriptDir);
    REQUIRE(mm::platform::fsWriteAtomic("/moonlive/unit-probe.mlp", "void setup() {}\n", 16));
    {
        mm::Drivers probe;                            // what serveModules builds…
        probe.defineControls();                       // …to read the control list…
    }                                                 // …and immediately destroys
    CHECK(mm::LivePalettes::count() == before);       // the new file waits for a scan, which a probe never runs
    REQUIRE(mm::LivePalettes::count() >= mm::moonlive::kPaletteCatalogCount);
    CHECK(std::strcmp(mm::LivePalettes::nameAt(0), mm::moonlive::kPaletteCatalog[0]) == 0);
    mm::platform::fsRemove("/moonlive/unit-probe.mlp");
}

// Regression: a saved script palette fell back to the first built-in at every boot, since the name was applied before the list it names was published.
TEST_CASE("a saved script palette's name resolves when the file is applied, before setup") {
    mm::LivePalettes::clear();
    mm::Drivers drivers;
    drivers.defineControls();
    const int i = mm::test::controlIndex(drivers, "palette");
    REQUIRE(i >= 0);
    char json[96];
    std::snprintf(json, sizeof(json), "{\"value\":\"%s\"}", mm::moonlive::kPaletteCatalog[0]);
    CHECK(mm::applyControlValue(drivers.controls()[static_cast<uint8_t>(i)], json, "value", mm::ApplyPolicy::Clamp)
          == mm::ApplyResult::Ok);
    CHECK(drivers.palette == mm::palettes::kCount);   // the first scripted entry, right after the built-ins
}

// Regression: clearing the palette script compiled the empty name, so the card said "no script: set the script name" while a built-in palette ran.
TEST_CASE("clearing the palette script leaves the Drivers card without an error") {
    mm::Drivers drivers;
    drivers.defineControls();
    const int i = mm::test::controlIndex(drivers, "paletteScript");
    REQUIRE(i >= 0);
    CHECK(mm::applyControlValue(drivers.controls()[static_cast<uint8_t>(i)], "{\"value\":\"\"}", "value",
                                mm::ApplyPolicy::Clamp) == mm::ApplyResult::Ok);
    drivers.onControlChanged("paletteScript");
    CHECK(drivers.status() == nullptr);
}

// A built-in palette drops the script palette's message and keeps any other, since the script, the relay and the encode worker share the card's one status.
TEST_CASE("a built-in palette clears the palette script's status and keeps another's") {
    mm::Drivers drivers;
    drivers.defineControls();
    drivers.palette = static_cast<uint8_t>(mm::palettes::kCount);   // the first scripted palette
    drivers.onControlChanged("palette");
    REQUIRE(drivers.status() != nullptr);                          // its compile result, or its error
    drivers.palette = 0;
    drivers.onControlChanged("palette");
    CHECK(drivers.status() == nullptr);

    drivers.palette = static_cast<uint8_t>(mm::palettes::kCount);
    drivers.onControlChanged("palette");
    drivers.setStatus("relay pin cannot drive an output", mm::MoonModule::Severity::Warning);
    drivers.palette = 0;
    drivers.onControlChanged("palette");
    REQUIRE(drivers.status() != nullptr);
    CHECK(std::strcmp(drivers.status(), "relay pin cannot drive an output") == 0);
}

// A strip at brightness 0 still draws idle current through a closed relay, so the relay opens at brightness 0 and closes when brightness returns.
TEST_CASE("the relay opens at brightness 0 and closes again when brightness returns") {
    mm::platform::clearTestGpioLevel();
    mm::Drivers drivers;
    std::strcpy(drivers.relayPins, "12");
    drivers.on = true;
    drivers.brightness = 100;
    drivers.onControlChanged("relayPins");            // entering the pin closes the relay at once
    CHECK(mm::platform::gpioRead(12));

    drivers.brightness = 0;
    drivers.onControlChanged("brightness");
    CHECK_FALSE(mm::platform::gpioRead(12));          // off by brightness, `on` still true

    drivers.brightness = 1;
    drivers.onControlChanged("brightness");
    CHECK(mm::platform::gpioRead(12));                // the smallest non-zero brightness is on

    drivers.on = false;
    drivers.onControlChanged("on");
    CHECK_FALSE(mm::platform::gpioRead(12));          // and `on` still opens it whatever brightness says
    mm::platform::clearTestGpioLevel();
}

TEST_CASE("generic relay control never writes a driver-owned power pin") {
    mm::platform::clearTestGpioLevel();
    mm::Drivers drivers;
    PowerPinOwner owner;
    REQUIRE(drivers.addChild(&owner));
    std::strcpy(drivers.relayPins, "17,18");
    drivers.on = true; drivers.brightness = 100;
    drivers.onControlChanged("relayPins");
    CHECK_FALSE(mm::platform::gpioRead(17));
    CHECK(mm::platform::gpioRead(18));
    CHECK(std::strcmp(drivers.status(), "relay pin is owned by a driver") == 0);
    // The owner may keep the reservation during disable/cleanup.
    owner.setEnabled(false);
    mm::platform::setTestGpioLevel(17, true);
    drivers.on = false; drivers.onControlChanged("on");
    CHECK(mm::platform::gpioRead(17));
    CHECK_FALSE(mm::platform::gpioRead(18));
    std::strcpy(drivers.relayPins, "17,,bad");
    drivers.onControlChanged("relayPins");
    CHECK(mm::platform::gpioRead(17));
    drivers.relayPins[0] = 0; drivers.onControlChanged("relayPins");
    CHECK(mm::platform::gpioRead(17));
    mm::platform::clearTestGpioLevel();
}

TEST_CASE("a newly reserved power pin is not written by the old relay owner") {
    mm::platform::clearTestGpioLevel();
    mm::Drivers drivers; PowerPinOwner owner; owner.reserved = false;
    REQUIRE(drivers.addChild(&owner));
    std::strcpy(drivers.relayPins, "17"); drivers.on = true; drivers.brightness = 100;
    drivers.onControlChanged("relayPins"); REQUIRE(mm::platform::gpioRead(17));
    owner.reserved = true;
    // Model the dedicated owner's separate low request, then a relay refresh.
    mm::platform::setTestGpioLevel(17, false);
    drivers.onControlChanged("relayPins"); CHECK_FALSE(mm::platform::gpioRead(17));
    mm::platform::setTestGpioLevel(17, true);
    drivers.relayPins[0] = 0; drivers.onControlChanged("relayPins");
    CHECK(mm::platform::gpioRead(17));
    mm::platform::clearTestGpioLevel();
}

// An unparseable relay list means no relays, the same as an empty one, so pins from the last valid list never stay asserted on GPIOs no control names.
TEST_CASE("a typo in the relay list releases the relays it used to hold") {
    mm::platform::clearTestGpioLevel();
    mm::Drivers drivers;
    std::strcpy(drivers.relayPins, "12,13");
    drivers.on = true;
    drivers.brightness = 100;
    drivers.onControlChanged("relayPins");
    CHECK(mm::platform::gpioRead(12));
    CHECK(mm::platform::gpioRead(13));

    // Mid-edit the list is briefly nonsense, which is the normal way a user types one.
    std::strcpy(drivers.relayPins, "12,,x");
    drivers.onControlChanged("relayPins");
    CHECK_FALSE(mm::platform::gpioRead(12));
    CHECK_FALSE(mm::platform::gpioRead(13));
    REQUIRE(drivers.status() != nullptr);
    CHECK(std::string(drivers.status()) == "invalid pin list");

    // And the driver has forgotten them, so a later brightness change does not resurrect either pin.
    drivers.brightness = 200;
    drivers.onControlChanged("brightness");
    CHECK_FALSE(mm::platform::gpioRead(12));
    CHECK_FALSE(mm::platform::gpioRead(13));

    // A corrected list takes effect normally, and the card stops naming the typo.
    std::strcpy(drivers.relayPins, "13");
    drivers.onControlChanged("relayPins");
    CHECK(mm::platform::gpioRead(13));
    CHECK_FALSE(mm::platform::gpioRead(12));
    CHECK((drivers.status() == nullptr || std::string(drivers.status()).empty()));
    mm::platform::clearTestGpioLevel();
}

// Removing the MIDDLE of three is what separates a real teardown from a truncation, since removeChild compacts in place.
TEST_CASE("Drivers: three sibling NetworkSendDrivers each own their buffer, and removing the middle one leaves the others driving") {
    mm::Buffer source;
    REQUIRE(source.allocate(64, 3));

    mm::Drivers drivers;
    mm::NetworkSendDriver first, middle, last;
    drivers.addChild(&first);
    drivers.addChild(&middle);
    drivers.addChild(&last);
    for (auto* d : {&first, &middle, &last}) {
        d->setSourceBuffer(&source);
        mm::test::rebuildFromPreset(d->correctionForTest(), 255, mm::test::PresetOrder::RGB);
        d->applyState();
    }

    // Three buffers, not one shared between them: a container that handed out one would show three equal pointers here.
    REQUIRE(first.correctedBuffer().data() != nullptr);
    REQUIRE(middle.correctedBuffer().data() != nullptr);
    REQUIRE(last.correctedBuffer().data() != nullptr);
    CHECK(first.correctedBuffer().data() != middle.correctedBuffer().data());
    CHECK(middle.correctedBuffer().data() != last.correctedBuffer().data());
    CHECK(first.correctedBuffer().data() != last.correctedBuffer().data());
    CHECK(drivers.childCount() == 3);

    const uint8_t* firstBefore = first.correctedBuffer().data();
    const uint8_t* lastBefore  = last.correctedBuffer().data();

    REQUIRE(drivers.removeChild(&middle));
    middle.release();

    // The survivors keep the buffers they were already sending from: the removal freed the one that went and touched neither of the others.
    CHECK(drivers.childCount() == 2);
    CHECK(first.correctedBuffer().data() == firstBefore);
    CHECK(last.correctedBuffer().data() == lastBefore);
    CHECK(first.correctedBuffer().count() == 64);
    CHECK(last.correctedBuffer().count() == 64);

    // The container's view is what a truncation breaks: reading the objects alone passes against a dropped LAST child.
    CHECK(drivers.child(0) == &first);
    CHECK(drivers.child(1) == &last);
    for (uint8_t i = 0; i < drivers.childCount(); i++) CHECK(drivers.child(i) != &middle);
}

// A user sees power, brightness and the look; the wiring an installer sets once waits for expert mode, and the firmware's own bench self-test for developer mode.
TEST_CASE("Drivers: relay pins and double buffering are expert settings, and the loopback test a developer one") {
    auto modeOf = [](mm::MoonModule& m, const char* name) {
        const int i = mm::test::controlIndex(m, name);
        REQUIRE(i >= 0);
        return m.controls()[static_cast<uint8_t>(i)].minMode;
    };
    mm::Drivers drivers;
    drivers.defineControls();
    CHECK(modeOf(drivers, "relayPins") == mm::kModeExpert);
    mm::ParallelLedDriver parallel;
    parallel.defineControls();
    CHECK(modeOf(parallel, "doubleBuffer") == mm::kModeExpert);
    for (const char* name : {"loopbackTest", "loopbackTxPin", "loopbackRxPin", "loopbackStrand", "loopbackIntrusive"})
        CHECK_MESSAGE(modeOf(parallel, name) == mm::kModeDeveloper, name);
    mm::RmtLedDriver rmt;
    rmt.defineControls();
    for (const char* name : {"loopbackTest", "loopbackTxPin", "loopbackRxPin", "loopbackFrame"})
        CHECK_MESSAGE(modeOf(rmt, name) == mm::kModeDeveloper, name);
}

// A scripted palette has no stored stops, so its color comes from the entries it computes now, not from the built-in its index would wrap onto.
TEST_CASE("Drivers gives a scripted palette's color from its live entries") {
    mm::Drivers drivers;
    const mm::Palette saved = *mm::Palettes::active();
    mm::Palette blue;
    for (auto& e : blue.entry) e = mm::RGB{0, 0, 255};
    mm::Palettes::setActiveDirect(blue);            // what a running palette script wrote
    uint16_t hue = 0, sat = 0;
    drivers.paletteHueSat(mm::palettes::kCount, hue, sat);   // the first scripted index
    CHECK(hue == 240);
    CHECK(sat == 255);
    uint16_t builtinHue = 0, builtinSat = 0;
    mm::Palettes::representativeHueSat(0, builtinHue, builtinSat);
    drivers.paletteHueSat(0, hue, sat);             // a built-in still reads its own stops
    CHECK(hue == builtinHue);
    CHECK(sat == builtinSat);
    mm::Palettes::setActiveDirect(saved);
}

namespace {
/// The palette picker's option count, which grows by one per scripted palette found.
uint8_t paletteOptions(mm::Drivers& d) {
    auto& cs = d.controls();
    for (uint8_t i = 0; i < cs.count(); i++)
        if (std::strcmp(cs[i].name, "palette") == 0) return static_cast<uint8_t>(cs[i].max);
    return 0;
}
}  // namespace

// Regression: every brightness change listed both script folders to rebuild the palette picker, 100 ms on an S3, so a desk fader froze the LEDs.
TEST_CASE("a brightness change does not rescan the palette scripts; a palette file written through the API does") {
    char root[64];
    std::snprintf(root, sizeof(root), "/tmp/mm_palette_scan_%u", static_cast<unsigned>(mm::platform::millis()));
    std::filesystem::remove_all(root);
    struct RootAfter { std::string prev = mm::platform::fsRootPath(); ~RootAfter() { mm::platform::fsSetRoot(prev.c_str()); } } rootAfter;
    mm::platform::fsSetRoot(root);
    mm::platform::fsMkdir("/moonlive");
    mm::Drivers drivers;
    drivers.defineControls();
    drivers.setup();                                 // the scan that counts, once the filesystem is up
    const uint8_t before = paletteOptions(drivers);

    const char script[] = "void tick() {}";
    REQUIRE(mm::platform::fsWriteAtomic("/moonlive/mine.mlp", script, sizeof(script) - 1));
    drivers.brightness = 90;
    drivers.rebuildControls();                       // what every brightness change does
    CHECK(paletteOptions(drivers) == before);        // no folder listing

    drivers.onFileChanged("/moonlive/mine.mlp");     // what a write through the API announces
    CHECK(paletteOptions(drivers) == before + 1);
    CHECK(mm::moonlive::pathTouchesFolder("/", "/moonlive"));             // a restore of everything
    CHECK_FALSE(mm::moonlive::pathTouchesFolder("/moonlivex/a.mlp", "/moonlive"));
    std::filesystem::remove_all(root);
}

// LEDs without data hold their last frame and its current, so safe mode sends black first, and holds the drivers only when the boots still fail.
TEST_CASE("safe mode darkens the lights, and holds the output drivers after four failed boots") {
    struct Record { ~Record() { mm::platform::setTestBootRecord({}); } } guard;
    mm::platform::setTestBootRecord({0, 2});
    {
        mm::Drivers drivers;
        CountingDriver output;
        drivers.addChild(&output);
        drivers.brightness = 200;
        drivers.setup();
        CHECK(drivers.effectiveBrightness() == 0);   // dark
        CHECK(output.enabled());                      // still sending, black
        drivers.removeChild(&output);
    }
    mm::platform::setTestBootRecord({0, 4});
    mm::Drivers drivers;
    CountingDriver output;
    mm::PreviewDriver preview;
    drivers.addChild(&output);
    drivers.addChild(&preview);
    CHECK_FALSE(output.enabled());
    CHECK(preview.enabled());
    drivers.removeChild(&preview);
    drivers.removeChild(&output);
}

// A device boot raises the lights over five seconds from the first frame, so a supply that cannot hold the level fails early, where the boot record counts it.
TEST_CASE("the lights rise to their brightness after a device boot, and a host process starts at it") {
    struct Record { ~Record() { mm::platform::setTestBootRecord({}); mm::platform::setTestNowMs(0); } } guard;
    mm::platform::setTestNowMs(1000);
    {
        mm::Drivers plain;
        plain.brightness = 200;
        plain.setup();
        CHECK(plain.effectiveBrightness() == 200);   // a host process, as the desktop app and every test
    }
    mm::platform::setTestBootRecord({0, 0, true});
    mm::Layouts layouts;
    mm::GridLayout grid;
    grid.width = 1; grid.height = 1;
    layouts.addChild(&grid);
    mm::Layer layer;
    layer.setLayouts(&layouts);
    layer.setChannelsPerLight(5);   // RGBW and one motion slot
    layouts.applyState();
    layer.applyState();
    mm::Drivers drivers;
    drivers.setLayer(&layer);
    drivers.brightness = 200;
    drivers.setup();
    drivers.tick20ms();
    CHECK(drivers.effectiveBrightness() == 0);   // dark until a light is lit
    REQUIRE(layer.buffer().data());
    layer.buffer().data()[mm::FixtureChannels::kMotionBase] = 255;   // an aim alone emits nothing, so the rise waits
    drivers.tick20ms();
    mm::platform::setTestNowMs(1000 + mm::Drivers::kSoftStartMs / 2);
    CHECK(drivers.effectiveBrightness() == 0);
    mm::platform::setTestNowMs(1000);
    layer.buffer().data()[0] = 255;
    drivers.tick20ms();   // the first lit frame starts the rise
    CHECK(drivers.effectiveBrightness() == 0);
    mm::platform::setTestNowMs(1000 + mm::Drivers::kSoftStartMs / 2);
    CHECK(drivers.effectiveBrightness() == 100);
    mm::platform::setTestNowMs(1000 + mm::Drivers::kSoftStartMs);
    drivers.tick20ms();
    CHECK(drivers.effectiveBrightness() == 200);
    layer.buffer().data()[0] = 0;
    mm::Drivers dark;   // a show that stays dark rises once the boot counts as good, and stops scanning for a lit frame
    dark.setLayer(&layer);
    dark.brightness = 200;
    dark.setup();
    mm::platform::setTestNowMs(mm::platform::kBootStableMs);
    dark.tick20ms();
    mm::platform::setTestNowMs(mm::platform::kBootStableMs + mm::Drivers::kSoftStartMs / 2);
    CHECK(dark.effectiveBrightness() == 100);
    layouts.removeChild(&grid);
}

// Regression: a palette script edited, or downloaded by an open page, stayed on its old program until a control changed.
TEST_CASE("the running palette script compiles again when its file changes") {
    mm::platform::fsMkdir(mm::moonlive::kScriptDir);
    const char* good = "class P {\n  void tick() { setPalEntry(0, 1, 2, 3); }\n}\n";
    REQUIRE(mm::platform::fsWriteAtomic("/moonlive/unit-live.mlp", good, std::strlen(good)));
    mm::Drivers drivers;
    drivers.defineControls();
    const int i = mm::test::controlIndex(drivers, "paletteScript");
    REQUIRE(i >= 0);
    REQUIRE(mm::applyControlValue(drivers.controls()[static_cast<uint8_t>(i)], "{\"value\":\"unit-live.mlp\"}",
                                  "value", mm::ApplyPolicy::Clamp) == mm::ApplyResult::Ok);
    drivers.onControlChanged("paletteScript");
    REQUIRE(drivers.status() != nullptr);
    CHECK(drivers.severity() == mm::MoonModule::Severity::Status);   // compiled: a status, not an error
    const char* broken = "class P {\n  void tick() { nosuch(); }\n}\n";
    REQUIRE(mm::platform::fsWriteAtomic("/moonlive/unit-live.mlp", broken, std::strlen(broken)));
    drivers.onFileChanged("/moonlive/unit-live.mlp");          // what a file write announces
    REQUIRE(drivers.status() != nullptr);
    CHECK(std::strstr(drivers.status(), "unknown function") != nullptr);
    CHECK(drivers.severity() == mm::MoonModule::Severity::Error);
    mm::platform::fsRemove("/moonlive/unit-live.mlp");
}
