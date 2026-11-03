/// @module HelioRemoteService
#include "doctest.h"
#include "helio/HelioRemoteService.h"
#include "light/drivers/FixtureProfilesModule.h"
#include "light/effects/SolidEffect.h"
#include "light/layers/Effects.h"
#include "light/layers/Layer.h"
#include <array>
#include <vector>

TEST_CASE("Remote timer adds to exact remaining duration and extends confirmation") {
    mm::RemoteTimer timer;
    CHECK(timer.add(100)); CHECK(timer.remainingMs == 1800000);
    timer.advance(1100); CHECK(timer.remainingMs == 1799000);
    CHECK(timer.add(2100)); CHECK(timer.remainingMs == 3598000);
    CHECK(timer.confirming(5099)); CHECK_FALSE(timer.confirming(5100));
    CHECK(timer.visible(6099)); CHECK_FALSE(timer.visible(6100));
    CHECK(timer.advance(3600100)); CHECK(timer.remainingMs == 0);
}
TEST_CASE("Timer preview uses fixed red to green sections and shrinks from the green end") {
    mm::RemoteTimer timer; std::array<uint8_t, 222 * 4> bytes;
    for (int i = 0; i < 6; ++i) REQUIRE(timer.cycle(0,180));
    REQUIRE(timer.render(bytes,4,3000));
    for (size_t i=0; i<222; ++i) {
        const size_t segment=i*18/222;
        CHECK(bytes[i*4] == 255-segment*255/17);
        CHECK(bytes[i*4+1] == segment*255/17);
        CHECK(bytes[i*4+2] == 0); CHECK(bytes[i*4+3] == 0);
    }
    CHECK(bytes[0] == 255); CHECK(bytes[1] == 0);
    CHECK(bytes[221*4] == 0); CHECK(bytes[221*4+1] == 255);
    REQUIRE(timer.render(bytes,4,250)); CHECK(bytes[0] == 0);
    timer.advance(150u*60000u);
    // Reopen the preview without changing the countdown.
    timer.add(150u*60000u); // Thirty minutes remaining plus thirty newly added.
    REQUIRE(timer.render(bytes,4,150u*60000u));
    CHECK(bytes[0] == 255); CHECK(bytes[1] == 0);
    CHECK(bytes[73*4] > 0); CHECK(bytes[74*4] == 0);
    REQUIRE(timer.render(bytes,4,150u*60000u+250));
    CHECK(bytes[0] == 255); CHECK(bytes[37*4] == 0);
    bytes.fill(99); CHECK_FALSE(timer.render(bytes,4,150u*60000u+4000)); CHECK(bytes[0] == 99);
}
TEST_CASE("Timer sections distribute arbitrary strip lengths and survive clock wrap") {
    for (size_t count : {1u,17u,18u,19u,222u,360u}) {
        mm::RemoteTimer timer; std::vector<uint8_t> bytes(count*4);
        timer.add(0xfffffff0u);
        REQUIRE(timer.render(bytes,4,0xfffffff0u+3000));
        const size_t lit=(count+5)/6;
        for (size_t i=0; i<count; ++i) CHECK((bytes[i*4] || bytes[i*4+1]) == (i<lit));
        CHECK_FALSE(timer.advance(0x10)); CHECK(timer.remainingMs == 1800000-32);
        timer.cancel(); CHECK_FALSE(timer.visible(0x10));
    }
}

namespace {
struct Handset : mm::HelioRemoteService {
    using HelioRemoteService::handleKey;
    using HelioRemoteService::acceptedKey;
    unsigned prepares = 0;
    void prepare() override { ++prepares; HelioRemoteService::prepare(); }
};
struct RemoteOutput : mm::DriverBase {
    void setSourceBuffer(mm::Buffer*) override {}
};
}
TEST_CASE("Remote white indication remains visible on RGB and native on RGBW") {
    mm::RemoteTimer timer;
    std::array<uint8_t, 27> rgb{};
    std::array<uint8_t, 36> rgbw{};
    timer.indicate(100, 0, 0, 0, 255);
    REQUIRE(timer.render(rgb, 3, 100));
    for (auto channel : rgb) CHECK(channel == 255);
    REQUIRE(timer.render(rgbw, 4, 100));
    for (size_t i = 0; i < rgbw.size(); ++i) CHECK(rgbw[i] == (i % 4 == 3 ? 255 : 0));
    CHECK_FALSE(timer.render(rgb, 3, 800));
}

TEST_CASE("Product handset white supports RGB strips alongside a separate white accent") {
    for (const char* fixture : {"RGB", "RGBW"}) {
        mm::FixtureProfilesModule profiles;
        profiles.setup();
        mm::Scheduler scheduler;
        mm::ModuleFactory::registerType<mm::Layer>("Layer");
        mm::ModuleFactory::registerType<mm::Drivers>("Drivers");
        mm::ModuleFactory::registerType<mm::SolidEffect>("SolidEffect");
        auto* effects = new mm::Effects(); effects->setName("Effects"); effects->setTypeName("Effects");
        auto* layer = new mm::Layer(); layer->setName("Layer"); layer->setTypeName("Layer"); layer->markWiredByCode();
        layer->setChannelsPerLight(4);
        effects->addChild(layer); effects->defineControls(); scheduler.addModule(effects);
        auto* drivers = new mm::Drivers(); drivers->setName("Drivers"); drivers->setTypeName("Drivers");
        auto* strip = new RemoteOutput(); strip->setDefaultCorrection(fixture, mm::WhiteMode::None);
        auto* accent = new RemoteOutput(); accent->setDefaultCorrection("White", mm::WhiteMode::None);
        strip->setDefaultCurve(mm::Correction::Curve::Linear);
        accent->setDefaultCurve(mm::Correction::Curve::Linear);
        drivers->addChild(strip); drivers->addChild(accent); drivers->defineControls(); scheduler.addModule(drivers);
        auto* remote = new Handset(); remote->setName("Remote"); scheduler.addModule(remote); scheduler.setup();
        drivers->fixtureChannels();
        REQUIRE(remote->handleKey(mm::RsKey::White, 100));
        auto* solid = dynamic_cast<mm::SolidEffect*>(scheduler.firstByName("BLE Scene")); REQUIRE(solid);
        const uint8_t expected = !std::strcmp(fixture, "RGB") ? 255 : 0;
        CHECK(solid->red == expected); CHECK(solid->green == expected); CHECK(solid->blue == expected); CHECK(solid->white == 255);
        uint8_t source[] = {solid->red, solid->green, solid->blue, solid->white};
        std::array<uint8_t, 4> output{};
        strip->rebuildCorrection(255); strip->correction().apply(source, output.data(), 4);
        for (uint8_t c = 0; c < strip->correction().outChannels; ++c)
            CHECK(output[c] == (!std::strcmp(fixture, "RGB") || c == strip->correction().offWhite ? 255 : 0));
        accent->rebuildCorrection(255); accent->correction().apply(source, output.data(), 4);
        CHECK(output[0] == 255);
        remote->musicMode = 2;
        REQUIRE(remote->handleKey(mm::RsKey::Music, 200));
        auto* timer = mm::ActiveInstance<mm::RemoteTimer>::active(); REQUIRE(timer);
        std::array<uint8_t, 36> indication{};
        REQUIRE(timer->render(indication, 4, 200));
        CHECK(indication[0] == expected); CHECK(indication[1] == expected); CHECK(indication[2] == expected); CHECK(indication[3] == 255);
        scheduler.release();
    }
}
TEST_CASE("HELIO timer commands extend countdown while OFF cancels and Music cycles three modes") {
    mm::Scheduler scheduler;
    auto* remote = new Handset(); remote->setName("Remote");
    scheduler.addModule(remote); scheduler.setup();
    REQUIRE(remote->handleKey(mm::RsKey::Timer, 100));
    auto* timer = mm::ActiveInstance<mm::RemoteTimer>::active(); REQUIRE(timer);
    CHECK(timer->remainingMs == 1800000);
    REQUIRE(remote->handleKey(mm::RsKey::Timer, 1100));
    CHECK(timer->remainingMs == 3599000);
    CHECK(remote->musicMode == 0);
    std::array<uint8_t, 888> indicator;
    CHECK(remote->handleKey(mm::RsKey::Music, 1200)); CHECK(remote->musicMode == 1);
    REQUIRE(timer->render(indicator,4,1200)); CHECK(indicator[0] == 0); CHECK(indicator[2] == 255); CHECK(indicator[3] == 0);
    CHECK(remote->handleKey(mm::RsKey::Music, 1300)); CHECK(remote->musicMode == 2);
    REQUIRE(timer->render(indicator,4,1300)); CHECK(indicator[0] == 160); CHECK(indicator[2] == 255); CHECK(indicator[3] == 0);
    CHECK(remote->handleKey(mm::RsKey::Music, 1400)); CHECK(remote->musicMode == 0);
    REQUIRE(timer->render(indicator,4,1400)); CHECK(indicator[0] == 0); CHECK(indicator[2] == 0); CHECK(indicator[3] == 255);
    remote->automatic = true;
    remote->acceptedKey(mm::RsKey::Off, 1200);
    CHECK_FALSE(remote->automatic); CHECK(timer->remainingMs == 0);
    scheduler.release(); CHECK(mm::ActiveInstance<mm::RemoteTimer>::active() == nullptr);
}

TEST_CASE("Auto toggles with a green or red indication and preserves the countdown") {
    mm::Scheduler scheduler;
    auto* remote = new Handset(); remote->setName("Remote");
    scheduler.addModule(remote); scheduler.setup();
    auto* timer = mm::ActiveInstance<mm::RemoteTimer>::active(); REQUIRE(timer);
    REQUIRE(timer->add(100));
    std::array<uint8_t,888> output;
    for (unsigned i=0; i<4; ++i) {
        const uint32_t now=200+i*1000;
        REQUIRE(remote->handleKey(mm::RsKey::Auto,now));
        CHECK(remote->automatic == (i%2 == 0));
        REQUIRE(timer->render(output,4,now));
        CHECK(output[0] == (remote->automatic ? 0 : 255));
        CHECK(output[1] == (remote->automatic ? 255 : 0));
        CHECK(output[2] == 0); CHECK(output[3] == 0);
        CHECK(timer->indicating(now+699)); CHECK_FALSE(timer->indicating(now+700));
        CHECK(timer->remainingMs == 1800000);
    }
    scheduler.release();
}


#include "light/effects/RingsEffect.h"
#include "light/effects/HelioWledPresetsEffect.h"
#include <set>
TEST_CASE("HELIO color keys use native solid RGBW and speed uses the live descriptor") {
    mm::Scheduler scheduler;
    mm::ModuleFactory::registerType<mm::Layer>("Layer");
    mm::ModuleFactory::registerType<mm::Drivers>("Drivers");
    mm::ModuleFactory::registerType<mm::SolidEffect>("SolidEffect");
    mm::ModuleFactory::registerType<mm::RingsEffect>("RingsEffect");
    auto* effects = new mm::Effects(); effects->setName("Effects"); effects->setTypeName("Effects");
    auto* layer = new mm::Layer(); layer->setName("Layer"); layer->setTypeName("Layer"); layer->markWiredByCode();
    layer->setChannelsPerLight(4);
    effects->addChild(layer); effects->defineControls(); scheduler.addModule(effects);
    auto* drivers = new mm::Drivers(); drivers->setName("Drivers"); drivers->setTypeName("Drivers"); drivers->defineControls(); scheduler.addModule(drivers);
    auto* remote = new Handset(); remote->setName("Remote"); scheduler.addModule(remote); scheduler.setup();
    for (auto key : {mm::RsKey::White, mm::RsKey::Yellow, mm::RsKey::Cyan, mm::RsKey::Pink, mm::RsKey::Red, mm::RsKey::Green, mm::RsKey::Blue}) {
        REQUIRE(remote->handleKey(key, 100));
        auto* effect = dynamic_cast<mm::SolidEffect*>(scheduler.firstByName("BLE Scene")); REQUIRE(effect);
        CHECK(effect->white == (key == mm::RsKey::White ? 255 : 0));
        CHECK(effect->red == (key == mm::RsKey::Yellow || key == mm::RsKey::Pink || key == mm::RsKey::Red ? 255 : 0));
        CHECK(effect->green == (key == mm::RsKey::Yellow || key == mm::RsKey::Cyan || key == mm::RsKey::Green ? 255 : 0));
        CHECK(effect->blue == (key == mm::RsKey::Cyan || key == mm::RsKey::Pink || key == mm::RsKey::Blue ? 255 : 0));
    }
    REQUIRE(remote->handleKey(mm::RsKey::ColorUp, 200));
    auto* solid = dynamic_cast<mm::SolidEffect*>(scheduler.firstByName("BLE Scene")); REQUIRE(solid); CHECK(solid->white == 0);
    for (uint32_t at : {1000u, 1100u, 1200u, 1300u}) REQUIRE(remote->handleKey(mm::RsKey::ColorUp, at));
    auto rgb = mm::hsvToRgb(195, 255, 255);
    solid = dynamic_cast<mm::SolidEffect*>(scheduler.firstByName("BLE Scene")); REQUIRE(solid);
    CHECK(solid->red == rgb.r); CHECK(solid->green == rgb.g); CHECK(solid->blue == rgb.b);
    REQUIRE(remote->handleKey(mm::RsKey::ColorDown, 1400));
    rgb = mm::hsvToRgb(192, 255, 255);
    solid = dynamic_cast<mm::SolidEffect*>(scheduler.firstByName("BLE Scene")); REQUIRE(solid);
    CHECK(solid->red == rgb.r); CHECK(solid->green == rgb.g); CHECK(solid->blue == rgb.b);
    REQUIRE(remote->handleKey(mm::RsKey::ColorDown, 2000));
    rgb = mm::hsvToRgb(189, 255, 255);
    solid = dynamic_cast<mm::SolidEffect*>(scheduler.firstByName("BLE Scene")); REQUIRE(solid);
    CHECK(solid->red == rgb.r); CHECK(solid->green == rgb.g); CHECK(solid->blue == rgb.b);
    REQUIRE(mm::BleSceneCatalog::apply(scheduler, "ripple", mm::BleSceneCatalog::revision(), "{}", true).ok);
    auto* rings = dynamic_cast<mm::RingsEffect*>(scheduler.firstByName("BLE Scene")); REQUIRE(rings);
    CHECK(rings->speed == 1);   // the catalog's Ripple speed, the descriptor's minimum
    REQUIRE(remote->handleKey(mm::RsKey::SpeedUp, 2100)); CHECK(rings->speed == 17);
    REQUIRE(remote->handleKey(mm::RsKey::SpeedDown, 2200)); CHECK(rings->speed == 1);
    scheduler.setControl("BLE Scene", "speed", "{\"value\":250}", true);
    REQUIRE(remote->handleKey(mm::RsKey::SpeedUp, 2300)); CHECK(rings->speed == 255);
    scheduler.setControl("BLE Scene", "speed", "{\"value\":2}", true);
    REQUIRE(remote->handleKey(mm::RsKey::SpeedDown, 2400)); CHECK(rings->speed == 1);
    scheduler.setControl("BLE Scene", "speed", "{\"value\":1}", true);
    for (uint32_t at : {3000u, 3100u, 3200u, 3300u}) REQUIRE(remote->handleKey(mm::RsKey::SpeedUp, at));
    CHECK(rings->speed == 113); // 16 + 16 + 16 + 64
    REQUIRE(remote->handleKey(mm::RsKey::SpeedDown, 3400)); CHECK(rings->speed == 97);
    REQUIRE(remote->handleKey(mm::RsKey::SpeedDown, 4000)); CHECK(rings->speed == 81);
    int32_t paletteBefore = 0, paletteAfter = 0;
    REQUIRE(scheduler.getControlWide("Drivers", "palette", paletteBefore));
    REQUIRE(remote->handleKey(mm::RsKey::ColorUp, 2500));
    REQUIRE(scheduler.getControlWide("Drivers", "palette", paletteAfter));
    CHECK(paletteAfter == paletteBefore + 1);
    CHECK(scheduler.firstByName("BLE Scene") == rings);
    REQUIRE(remote->handleKey(mm::RsKey::ColorDown, 2600));
    REQUIRE(scheduler.getControlWide("Drivers", "palette", paletteAfter));
    CHECK(paletteAfter == paletteBefore);
    scheduler.release();
}

TEST_CASE("Queued remote timer packets use monotonic processing time after the current tick") {
    mm::Scheduler scheduler;
    auto* remote = new Handset(); remote->setName("Remote"); scheduler.addModule(remote); scheduler.setup();
    std::array<uint8_t, 24> bytes{};
    const char* hex = "525302000e0c000e0c000501d8a87ef30202ff0403002900";
    for (size_t i = 0; i < bytes.size(); ++i) { unsigned b = 0; std::sscanf(hex + 2*i, "%2x", &b); bytes[i] = static_cast<uint8_t>(b); }
    remote->injectAdvertisementForTest(bytes, 100, 110);
    auto* timer = mm::ActiveInstance<mm::RemoteTimer>::active(); REQUIRE(timer);
    CHECK(timer->remainingMs == 1800000);
    timer->advance(200); ++bytes[14];
    remote->injectAdvertisementForTest(bytes, 195, 205);
    CHECK(timer->remainingMs == 3600000 - 95);
    scheduler.release();
}


#include <filesystem>
#include <cstdlib>
TEST_CASE("HELIO Auto visits every native preset and Mode wraps both directions") {
    char root[] = "/tmp/helio-remote-presets-XXXXXX";
    REQUIRE(mkdtemp(root) != nullptr);
    struct Cleanup { const char* path; ~Cleanup() { std::filesystem::remove_all(path); } } cleanup{root};
    mm::platform::fsSetRoot(root);
    mm::Scheduler scheduler;
    mm::ModuleFactory::registerType<mm::Effects>("Effects");
    mm::ModuleFactory::registerType<mm::Layer>("Layer");
    mm::ModuleFactory::registerType<mm::SolidEffect>("SolidEffect");
    mm::ModuleFactory::registerType<mm::Drivers>("Drivers");
    auto* fs = new mm::FilesystemModule(); fs->setTypeName("FilesystemModule"); fs->setScheduler(&scheduler);
    scheduler.addModule(fs);
    auto* effects = new mm::Effects(); effects->setName("Effects"); effects->setTypeName("Effects"); scheduler.addModule(effects);
    auto* drivers = new mm::Drivers(); drivers->setName("Drivers"); drivers->setTypeName("Drivers"); scheduler.addModule(drivers);
    auto* control = new mm::ControlModule(); control->setName("Control"); control->setTypeName("ControlModule"); scheduler.addModule(control);
    auto* remote = new Handset(); remote->setName("Remote"); scheduler.addModule(remote); scheduler.setup();
    REQUIRE(remote->autoIntervalSeconds == 30);
    REQUIRE(remote->timerMaximumMinutes == 180);
    mm::platform::fsMkdir(mm::ControlModule::kPresetDir);
    for (unsigned i = 0; i < 24; ++i) {
        char path[160], document[320];
        std::snprintf(path, sizeof(path), "%s/Preset%02u.json", mm::ControlModule::kPresetDir, i);
        std::snprintf(document, sizeof(document), R"({"Effects":{"$patch":"replace","enabled":true,"Layer":{"type":"Layer","BLE Scene":{"type":"SolidEffect","red":%u}}}})", i);
        REQUIRE(mm::platform::fsWriteAtomic(path, document, std::strlen(document)));
    }
    control->onFileChanged(mm::ControlModule::kPresetDir);
    REQUIRE(control->presetCount() == 24);
    std::array<std::string, 24> order;
    for (uint8_t i = 0; i < order.size(); ++i) order[i] = control->presetName(i);
    REQUIRE(remote->handleKey(mm::RsKey::Auto, mm::platform::millis() - 700));
    CHECK(std::strcmp(control->currentLook(), order[0].c_str()) == 0);
    remote->autoIntervalSeconds = 0; // Exercise each scheduler transition without a twelve-minute wall-clock test.
    for (unsigned i = 1; i <= 24; ++i) {
        remote->tick();
        CHECK(std::strcmp(control->currentLook(), order[i % 24].c_str()) == 0);
        CHECK(remote->automatic);
    }
    REQUIRE(remote->handleKey(mm::RsKey::ModeDown, mm::platform::millis()));
    CHECK_FALSE(remote->automatic); CHECK(std::strcmp(control->currentLook(), order[23].c_str()) == 0);
    REQUIRE(remote->handleKey(mm::RsKey::ModeUp, mm::platform::millis()));
    CHECK(std::strcmp(control->currentLook(), order[0].c_str()) == 0);
    mm::ModuleFactory::registerType<mm::HelioStarburstEffect>("HelioStarburstEffect");
    for (unsigned i = 1; i < 24; i += 2) {
        char path[160], document[320];
        std::snprintf(path, sizeof(path), "%s/Preset%02u.json", mm::ControlModule::kPresetDir, i);
        std::snprintf(document, sizeof(document), R"({"Effects":{"$patch":"replace","enabled":true,"Layer":{"type":"Layer","BLE Scene":{"type":"HelioStarburstEffect","red":%u}}}})", i);
        REQUIRE(mm::platform::fsWriteAtomic(path, document, std::strlen(document)));
    }
    control->onFileChanged(mm::ControlModule::kPresetDir);
    for (uint8_t mode = 0; mode < 3; ++mode) {
        remote->musicMode = mode; remote->automatic = false;
        std::set<std::string> seen, expected;
        for (unsigned i = 0; i < 24; ++i) {
            if (mode != 2 && (i % 2 != unsigned(mode))) continue;
            char name[20]; std::snprintf(name, sizeof(name), "Preset%02u", i); expected.insert(name);
        }
        REQUIRE(remote->handleKey(mm::RsKey::Auto, mm::platform::millis() - 700));
        for (unsigned i = 0; i < 24; ++i) {
            seen.insert(control->currentLook());
            CHECK(expected.contains(control->currentLook()));
            remote->tick();
        }
        CHECK(seen == expected);
    }
    const unsigned initialPrepares = remote->prepares;
    // Exercise the actual decoded Music packets through preset changes and queued preparations.
    remote->automatic = false; remote->musicMode = 0;
    std::array<uint8_t,24> packet{};
    const char* hex = "525302000e0c000e0c000501af947b310202ff0403001202";
    for (size_t i=0; i<packet.size(); ++i) { unsigned byte=0; std::sscanf(hex+2*i,"%2x",&byte); packet[i]=byte; }
    std::array<uint8_t,888> output;
    for (uint8_t expected : {1,2,0,1,2,0}) {
        ++packet[14];
        const uint32_t now=mm::platform::millis();
        remote->injectAdvertisementForTest(packet,now);
        scheduler.preparePending();
        CHECK(remote->musicMode == expected);
        auto* overlay=mm::ActiveInstance<mm::RemoteTimer>::active(); REQUIRE(overlay);
        REQUIRE(overlay->render(output,4,now));
        // These saved presets create RGB layers without a physical RGBW fixture.
        CHECK(output[0] == (expected == 0 ? 255 : expected == 2 ? 160 : 0));
        CHECK(output[2] == 255);
        CHECK(output[3] == (expected == 0 ? 255 : 0));
        remote->injectAdvertisementForTest(packet,now+100);
        CHECK(remote->musicMode == expected);
    }
    CHECK(remote->prepares == initialPrepares);
    scheduler.release();
}

TEST_CASE("Music indication uses dedicated warm white, blue and purple then restores the output") {
    mm::RemoteTimer overlay; std::array<uint8_t, 222 * 4> bytes;
    for (auto color : {std::array<uint8_t,4>{0,0,0,255}, {0,0,255,0}, {160,0,255,0}}) {
        overlay.indicate(0xfffffff0, color[0], color[1], color[2], color[3]);
        CHECK(overlay.render(bytes, 4, 0xfffffff0u + 699));
        for (unsigned c=0; c<4; ++c) { CHECK(bytes[c] == color[c]); CHECK(bytes[221*4+c] == color[c]); }
        bytes.fill(99); CHECK_FALSE(overlay.render(bytes, 4, 0xfffffff0u + 700)); CHECK(bytes[0] == 99);
    }
    overlay.indicate(0,0,0,255,0); overlay.cancel(); CHECK_FALSE(overlay.indicating(0));
}

TEST_CASE("Timer cycles from its configured limit through unset back to thirty minutes") {
    for (uint16_t limit : {120,180,240}) {
        mm::RemoteTimer timer; std::array<uint8_t, 222 * 4> bytes;
        for (uint16_t minutes = 30; minutes <= limit; minutes += 30) {
            REQUIRE(timer.cycle(0,limit)); CHECK(timer.minutes() == minutes);
        }
        REQUIRE(timer.cycle(1000,limit)); CHECK(timer.remainingMs == 0);
        CHECK_FALSE(timer.advance(1100));
        REQUIRE(timer.render(bytes,4,1100));
        CHECK(std::all_of(bytes.begin(),bytes.end(),[](uint8_t byte) { return byte == 0; }));
        bytes.fill(99); CHECK_FALSE(timer.render(bytes,4,1700)); CHECK(bytes[0] == 99);
        REQUIRE(timer.cycle(2000,limit)); CHECK(timer.remainingMs == 1800000);
    }
}

TEST_CASE("Brightness accelerates accepted repeats, clamps and resets on direction or pause") {
    struct Surface : mm::MoonModule {
        uint8_t fader1=1;
        void defineControls() override { controls_.addControl("fader1",fader1,0,255); }
    };
    mm::Scheduler scheduler;
    auto* surface=new Surface(); surface->setName("Control"); scheduler.addModule(surface);
    auto* remote=new Handset(); remote->setName("Remote"); scheduler.addModule(remote); scheduler.setup();
    std::array<uint8_t,24> packet{};
    const char* hex="525302000e0c000e0c000501b9a85fd40202ff0403000901";
    for (size_t i=0; i<packet.size(); ++i) { unsigned byte=0; std::sscanf(hex+2*i,"%2x",&byte); packet[i]=byte; }
    for (uint32_t now : {1000u,1100u,1200u,1300u}) {
        ++packet[14]; remote->injectAdvertisementForTest(packet,now);
        const auto before=surface->fader1;
        remote->injectAdvertisementForTest(packet,now+1); CHECK(surface->fader1 == before);
    }
    CHECK(surface->fader1 == 113);
    ++packet[14]; packet[23]=0; remote->injectAdvertisementForTest(packet,1400); CHECK(surface->fader1 == 97);
    ++packet[14]; remote->injectAdvertisementForTest(packet,2000); CHECK(surface->fader1 == 81);
    for (uint32_t now : {2100u,2200u,2300u}) { ++packet[14]; remote->injectAdvertisementForTest(packet,now); }
    CHECK(surface->fader1 == 0);
    packet[23]=1; surface->fader1=250; ++packet[14]; remote->injectAdvertisementForTest(packet,2400);
    CHECK(surface->fader1 == 255);
    scheduler.release();
}

TEST_CASE("Delay reopens a running timer without adding time, then subsequent presses extend it") {
    mm::Scheduler scheduler;
    auto* remote=new Handset(); remote->setName("Remote"); scheduler.addModule(remote); scheduler.setup();
    auto* timer=mm::ActiveInstance<mm::RemoteTimer>::active(); REQUIRE(timer);
    REQUIRE(remote->handleKey(mm::RsKey::Timer,1000)); CHECK(timer->remainingMs == 1800000);
    REQUIRE(remote->handleKey(mm::RsKey::Timer,5000)); CHECK(timer->remainingMs == 1796000);
    std::array<uint8_t,888> output;
    REQUIRE(timer->render(output,4,5250)); CHECK(output[0] == 255); // Current time stays steady.
    REQUIRE(remote->handleKey(mm::RsKey::Timer,5500)); CHECK(timer->remainingMs == 3595500);
    REQUIRE(timer->render(output,4,5750)); CHECK(output[0] == 255); CHECK(output[37*4] == 0); // Only added time blinks.
    REQUIRE(remote->handleKey(mm::RsKey::Timer,9500)); CHECK(timer->remainingMs == 3591500);
    timer->cancel();
    for (unsigned i=0; i<6; ++i) REQUIRE(remote->handleKey(mm::RsKey::Timer,10000+i*100));
    REQUIRE(remote->handleKey(mm::RsKey::Timer,15000)); CHECK(timer->remainingMs > 0); // First press at cap only previews.
    REQUIRE(remote->handleKey(mm::RsKey::Timer,15100)); CHECK(timer->remainingMs == 0);
    REQUIRE(remote->handleKey(mm::RsKey::Timer,15200)); CHECK(timer->remainingMs == 1800000);
    timer->advance(1815200);
    REQUIRE(remote->handleKey(mm::RsKey::Timer,1815200)); CHECK(timer->remainingMs == 1800000);
    scheduler.release();
}
