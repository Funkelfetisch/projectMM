/// @module BleSceneCatalog
#include <map>
#include "doctest.h"
#include "helio/BleSceneCatalog.h"
#include "core/services/BleControlService.h"
#include "light/drivers/Drivers.h"
#include "light/effects/HelioWledPresetsEffect.h"
#include "light/effects/SolidEffect.h"
#include "light/effects/NebuliteRainbowEffect.h"
#include "light/effects/NebuliteBlinkEffect.h"
#include "light/effects/NebuliteSolidEffect.h"
#include "light/effects/NebuliteSineEffect.h"
#include "light/effects/NebuliteWashingEffect.h"
#include "light/effects/NebulitePaletteEffect.h"
#include "light/effects/NebuliteFireflyEffect.h"
#include "light/effects/NebuliteSnowflakesEffect.h"
#include "light/effects/NebuliteFlowStripeEffect.h"
#include "light/effects/NebulitePlasmoidEffect.h"
#include "light/effects/NebuliteNoisefireEffect.h"
#include "light/effects/RingsEffect.h"
#include "light/effects/LavaLampEffect.h"
#include "light/effects/DistortionWavesEffect.h"
#include "light/effects/PlasmaEffect.h"
#include "light/effects/MetaballsEffect.h"
#include "light/layers/Effects.h"
#include "light/layers/Layer.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {
struct SceneTree {
    mm::Scheduler scheduler;
    mm::Effects* effects = new mm::Effects();
    mm::Layer* layer = new mm::Layer();
    mm::Drivers* drivers = new mm::Drivers();
    SceneTree() {
        mm::ModuleFactory::registerType<mm::Layer>("Layer");
        mm::ModuleFactory::registerType<mm::Drivers>("Drivers");
        mm::ModuleFactory::registerType<mm::SolidEffect>("SolidEffect");
        mm::ModuleFactory::registerType<mm::NebuliteRainbowEffect>("NebuliteRainbowEffect");
        mm::ModuleFactory::registerType<mm::NebuliteBlinkEffect>("NebuliteBlinkEffect");
        mm::ModuleFactory::registerType<mm::NebuliteSolidEffect>("NebuliteSolidEffect");
        mm::ModuleFactory::registerType<mm::NebuliteSineEffect>("NebuliteSineEffect");
        mm::ModuleFactory::registerType<mm::NebuliteWashingEffect>("NebuliteWashingEffect");
        mm::ModuleFactory::registerType<mm::NebulitePaletteEffect>("NebulitePaletteEffect");
        mm::ModuleFactory::registerType<mm::NebuliteFireflyEffect>("NebuliteFireflyEffect");
        mm::ModuleFactory::registerType<mm::NebuliteSnowflakesEffect>("NebuliteSnowflakesEffect");
        mm::ModuleFactory::registerType<mm::NebuliteFlowStripeEffect>("NebuliteFlowStripeEffect");
        mm::ModuleFactory::registerType<mm::NebulitePlasmoidEffect>("NebulitePlasmoidEffect");
        mm::ModuleFactory::registerType<mm::NebuliteNoisefireEffect>("NebuliteNoisefireEffect");
        mm::ModuleFactory::registerType<mm::RingsEffect>("RingsEffect");
        mm::ModuleFactory::registerType<mm::LavaLampEffect>("LavaLampEffect");
        mm::ModuleFactory::registerType<mm::DistortionWavesEffect>("DistortionWavesEffect");
        mm::ModuleFactory::registerType<mm::PlasmaEffect>("PlasmaEffect");
        mm::ModuleFactory::registerType<mm::MetaballsEffect>("MetaballsEffect");
        mm::ModuleFactory::registerType<mm::HelioCandleMultiEffect>("HelioCandleMultiEffect");
        mm::ModuleFactory::registerType<mm::HelioColorTwinklesEffect>("HelioColorTwinklesEffect");
        mm::ModuleFactory::registerType<mm::HelioFreqMapAmberEffect>("HelioFreqMapAmberEffect");
        mm::ModuleFactory::registerType<mm::HelioLakeEffect>("HelioLakeEffect");
        mm::ModuleFactory::registerType<mm::HelioLoadingEffect>("HelioLoadingEffect");
        mm::ModuleFactory::registerType<mm::HelioPinkPaletteEffect>("HelioPinkPaletteEffect");
        mm::ModuleFactory::registerType<mm::HelioPhasedEffect>("HelioPhasedEffect");
        mm::ModuleFactory::registerType<mm::HelioRunningEffect>("HelioRunningEffect");
        mm::ModuleFactory::registerType<mm::HelioStarburstEffect>("HelioStarburstEffect");
        mm::ModuleFactory::registerType<mm::HelioGravCenterEffect>("HelioGravCenterEffect");
        mm::ModuleFactory::registerType<mm::HelioJugglesEffect>("HelioJugglesEffect");
        mm::ModuleFactory::registerType<mm::HelioMultiCometEffect>("HelioMultiCometEffect");
        mm::ModuleFactory::registerType<mm::HelioNoiseMeterEffect>("HelioNoiseMeterEffect");
        mm::ModuleFactory::registerType<mm::HelioPlasmoidEffect>("HelioPlasmoidEffect");
        mm::ModuleFactory::registerType<mm::HelioFreqMapWideEffect>("HelioFreqMapWideEffect");
        mm::ModuleFactory::registerType<mm::HelioGEQEffect>("HelioGEQEffect");
        mm::ModuleFactory::registerType<mm::HelioGravFreqEffect>("HelioGravFreqEffect");
        mm::ModuleFactory::registerType<mm::HelioFreqWaveEffect>("HelioFreqWaveEffect");
        effects->setName("Effects"); effects->setTypeName("Effects");
        layer->setName("Layer"); layer->setTypeName("Layer"); layer->markWiredByCode();
        drivers->setName("Drivers"); drivers->setTypeName("Drivers");
        effects->addChild(layer); effects->defineControls(); drivers->defineControls();
        scheduler.addModule(effects); scheduler.addModule(drivers);
    }
    ~SceneTree() { scheduler.release(); }
    mm::StateDocumentResult apply(const char* id, const char* controls = "{}") {
        return mm::BleSceneCatalog::apply(scheduler, id, mm::BleSceneCatalog::revision(), controls, true);
    }
    std::string state() {
        char bytes[513]; mm::JsonSink sink(bytes, sizeof(bytes)); sink.append("{");
        mm::BleSceneCatalog::writeState(sink, scheduler); sink.append("}");
        REQUIRE_FALSE(sink.overflowed()); return bytes;
    }
};

void envelope(mm::JsonSink& sink) {
    sink.append("{\"v\":1,\"id\":65535,\"ok\":true,\"e\":\"ffffffffffffffffffffffffffffffff\",\"s\":4294967295,\"r\":4294967295,\"now\":4294967295,\"op\":\"scene-state\",");
}
}

static bool anyDirty(mm::MoonModule* m) {
    if (m->dirty()) return true;
    for (uint8_t i = 0; i < m->childCount(); ++i) if (anyDirty(m->child(i))) return true;
    return false;
}

TEST_CASE("A scene picked to keep is saved and one played live is not") {
    SceneTree tree;
    static int saves = 0;
    saves = 0;
    tree.scheduler.setNoteDirtyHook([] { ++saves; });
    REQUIRE(mm::BleSceneCatalog::apply(tree.scheduler, "nebulite-rainbow", mm::BleSceneCatalog::revision(), "{}", true).ok);
    CHECK(saves == 0);
    CHECK_FALSE(anyDirty(tree.effects));
    REQUIRE(mm::BleSceneCatalog::apply(tree.scheduler, "nebulite-blink", mm::BleSceneCatalog::revision(), "{}", false).ok);
    CHECK(saves > 0);
    CHECK(anyDirty(tree.effects));
}

TEST_CASE("BLE scenes: the shared catalog has stable IDs and bounded real control metadata") {
    SceneTree tree;
    CHECK(mm::BleSceneCatalog::count() == 35);
    CHECK(mm::BleSceneCatalog::exists("nebulite-noisefire"));
    CHECK(mm::BleSceneCatalog::exists("nebulite-plasmoid"));
    CHECK(mm::BleSceneCatalog::exists("nebulite-flow-stripe"));
    CHECK(mm::BleSceneCatalog::exists("nebulite-snowflakes"));
    CHECK(mm::BleSceneCatalog::exists("nebulite-firefly"));
    CHECK(mm::BleSceneCatalog::exists("solid")); CHECK(mm::BleSceneCatalog::exists("colortwinkles"));
    CHECK_FALSE(mm::BleSceneCatalog::exists("not-a-scene"));
    for (uint16_t i = 0; i < mm::BleSceneCatalog::count(); ++i) {
        char bytes[513]; mm::JsonSink sink(bytes, sizeof(bytes)); envelope(sink);
        REQUIRE(mm::BleSceneCatalog::writePage(sink, i) == nullptr); sink.append("}");
        CHECK_FALSE(sink.overflowed());
        mm::json::JsonDoc page; REQUIRE(mm::json::parse(bytes, page));
        const auto* scenes = mm::json::member(page, page.rootNode(), "scenes"); REQUIRE(scenes != nullptr);
        const auto* scene = &page.nodes[scenes->firstChild];
        const auto* id = mm::json::member(page, scene, "id"); REQUIRE(id != nullptr);
        REQUIRE(tree.apply(id->str).ok);
        char current[513]; mm::JsonSink state(current, sizeof(current)); envelope(state);
        mm::BleSceneCatalog::writeState(state, tree.scheduler); state.append("}");
        CHECK_FALSE(state.overflowed());
        mm::json::JsonDoc actual; REQUIRE(mm::json::parse(current, actual));
        const auto* recognized = mm::json::member(actual, actual.rootNode(), "scene"); REQUIRE(recognized != nullptr);
        REQUIRE(recognized->type == mm::json::JsonType::Object);
        CHECK(std::strcmp(mm::json::member(actual, recognized, "id")->str, id->str) == 0);
        for (uint16_t cursor = 0; cursor < 256;) {
            char detail[513]; mm::JsonSink output(detail, sizeof(detail)); envelope(output);
            REQUIRE(mm::BleSceneCatalog::writeInfo(output, id->str, cursor) == nullptr); output.append("}");
            CHECK_FALSE(output.overflowed()); mm::json::JsonDoc info; REQUIRE(mm::json::parse(detail, info));
            const auto* next = mm::json::member(info, info.rootNode(), "next"); REQUIRE(next != nullptr);
            if (next->type == mm::json::JsonType::Null) break;
            REQUIRE(next->intValue > cursor); cursor = static_cast<uint16_t>(next->intValue);
        }
    }
}

TEST_CASE("BLE scenes: a control the catalogue names carries its label, so the app shows Intensity rather than effect intensity") {
    std::map<std::string, std::string> labels;
    for (uint16_t cursor = 0; cursor < 256;) {
        char bytes[513]; mm::JsonSink sink(bytes, sizeof(bytes)); envelope(sink);
        REQUIRE(mm::BleSceneCatalog::writeInfo(sink, "candle-multi", cursor) == nullptr); sink.append("}");
        REQUIRE_FALSE(sink.overflowed());
        mm::json::JsonDoc info; REQUIRE(mm::json::parse(bytes, info));
        const auto* controls = mm::json::member(info, info.rootNode(), "controls"); REQUIRE(controls != nullptr);
        for (const auto* control = info.node(controls->firstChild); control; control = info.node(control->next)) {
            const auto* name = mm::json::member(info, control, "name"), *label = mm::json::member(info, control, "label");
            if (name && label) labels[name->str] = label->str;
        }
        const auto* next = mm::json::member(info, info.rootNode(), "next");
        if (!next || next->type == mm::json::JsonType::Null) break;
        cursor = static_cast<uint16_t>(std::atoi(next->str));
    }
    CHECK(labels["speed"] == "Speed");
    CHECK(labels["effect intensity"] == "Intensity");
    CHECK(labels["output"] == "Output level");
    CHECK(labels.count("palette") == 0);
}

TEST_CASE("BLE scenes: a brightness descriptor says it is a percent level, a colour channel does not") {
    SceneTree tree;
    std::string info;
    for (uint16_t cursor = 0; cursor < 256;) {
        char bytes[513]; mm::JsonSink sink(bytes, sizeof(bytes)); envelope(sink);
        REQUIRE(mm::BleSceneCatalog::writeInfo(sink, "solid", cursor) == nullptr); sink.append("}");
        REQUIRE_FALSE(sink.overflowed());
        info += bytes;
        mm::json::JsonDoc page; REQUIRE(mm::json::parse(bytes, page));
        const auto* next = mm::json::member(page, page.rootNode(), "next"); REQUIRE(next != nullptr);
        if (next->type == mm::json::JsonType::Null) break;
        cursor = static_cast<uint16_t>(next->intValue);
    }
    const auto brightness = info.find("\"name\":\"brightness\"");
    REQUIRE(brightness != std::string::npos);
    CHECK(info.find("\"percent\":true", brightness) != std::string::npos);
    const auto red = info.find("\"name\":\"red\"");
    REQUIRE(red != std::string::npos);
    CHECK(info.substr(red, info.find('}', red) - red).find("percent") == std::string::npos);
}

TEST_CASE("BLE scenes: solid replaces competing effects, modifiers and extra layers as one complete scene") {
    SceneTree tree;
    auto* wired = new mm::SolidEffect(); wired->setName("Wired"); wired->setTypeName("SolidEffect"); wired->markWiredByCode(); wired->defineControls(); tree.layer->addChild(wired);
    struct Modifier : mm::MoonModule { mm::ModuleRole role() const MM_NONBLOCKING override { return mm::ModuleRole::Modifier; } };
    auto* modifier = new Modifier(); modifier->setName("Warp"); modifier->setTypeName("Modifier"); tree.layer->addChild(modifier);
    auto* other = new mm::Layer(); other->setName("Other"); other->setTypeName("Layer"); other->markWiredByCode(); other->defineControls(); tree.effects->addChild(other);
    auto* rival = new mm::SolidEffect(); rival->setName("Rival"); rival->setTypeName("SolidEffect"); rival->defineControls(); other->addChild(rival);
    const auto result = tree.apply("solid", R"({"red":10,"green":20,"blue":30,"white":40})");
    REQUIRE(result.ok); CHECK_FALSE(wired->enabled()); CHECK_FALSE(other->enabled());
    CHECK(tree.scheduler.firstByName("Warp") == nullptr);
    auto* active = tree.scheduler.firstByName("BLE Scene"); REQUIRE(active != nullptr);
    auto* solid = static_cast<mm::SolidEffect*>(active);
    CHECK(solid->red == 10); CHECK(solid->green == 20); CHECK(solid->blue == 30); CHECK(solid->white == 40);
    CHECK(tree.state().find("\"id\":\"solid\"") != std::string::npos);
    solid->colorMode = mm::SolidEffect::kColorPalette;
    CHECK(tree.state() == "{\"scene\":null}");
}

TEST_CASE("BLE scenes: selecting the same scene resets all non-exposed settings and reports actual controls") {
    SceneTree tree; REQUIRE(tree.apply("colortwinkles").ok);
    auto* active = static_cast<mm::HelioColorTwinklesEffect*>(tree.scheduler.firstByName("BLE Scene")); REQUIRE(active != nullptr);
    active->idleBeat = true; CHECK(tree.state() == "{\"scene\":null}");
    REQUIRE(tree.apply("colortwinkles", R"({"speed":41,"white":29,"palette":0})").ok);
    CHECK_FALSE(active->idleBeat); CHECK(active->speed == 41); CHECK(active->white == 29);
    const auto current = tree.state(); CHECK(current.find("\"speed\":41") != std::string::npos); CHECK(current.find("\"white\":29") != std::string::npos);
    REQUIRE(tree.apply("solid").ok); CHECK(tree.state().find("\"id\":\"solid\"") != std::string::npos);
}

TEST_CASE("BLE scenes: Rings background is an exposed bounded byte and fits complete RPC-sized state and metadata") {
    SceneTree tree;
    tree.drivers->brightness = 137;
    REQUIRE(tree.apply("ripple").ok);
    auto* module = tree.scheduler.firstByName("BLE Scene");
    REQUIRE(module != nullptr);
    REQUIRE(std::strcmp(module->typeName(), "RingsEffect") == 0);
    CHECK(static_cast<mm::RingsEffect*>(module)->backgroundBrightness == 0);
    bool exposed = false;
    for (uint16_t cursor = 0; cursor < 256;) {
        char bytes[513]; mm::JsonSink sink(bytes, sizeof(bytes)); envelope(sink);
        REQUIRE(mm::BleSceneCatalog::writeInfo(sink, "ripple", cursor) == nullptr);
        sink.append("}");
        REQUIRE_FALSE(sink.overflowed());
        CHECK(std::strlen(bytes) <= 512);
        mm::json::JsonDoc info;
        REQUIRE(mm::json::parse(bytes, info));
        const auto* controls = mm::json::member(info, info.rootNode(), "controls");
        REQUIRE(controls != nullptr);
        for (const auto* control = info.node(controls->firstChild); control; control = info.node(control->next)) {
            const auto* name = mm::json::member(info, control, "name");
            REQUIRE(name != nullptr);
            if (std::strcmp(name->str, "background brightness")) continue;
            CHECK_FALSE(exposed); exposed = true;
            const auto* type = mm::json::member(info, control, "type");
            const auto* minimum = mm::json::member(info, control, "min");
            const auto* maximum = mm::json::member(info, control, "max");
            const auto* initial = mm::json::member(info, control, "default");
            REQUIRE(type != nullptr); REQUIRE(minimum != nullptr); REQUIRE(maximum != nullptr); REQUIRE(initial != nullptr);
            CHECK(std::strcmp(type->str, "uint8") == 0);
            CHECK(minimum->intValue == 0);
            CHECK(maximum->intValue == 255);
            CHECK(initial->intValue == 0);
        }
        const auto* next = mm::json::member(info, info.rootNode(), "next");
        REQUIRE(next != nullptr);
        if (next->type == mm::json::JsonType::Null) break;
        REQUIRE(next->intValue > cursor);
        cursor = static_cast<uint16_t>(next->intValue);
    }
    REQUIRE(exposed);
    REQUIRE(tree.apply("ripple", R"({"background brightness":255})").ok);
    module = tree.scheduler.firstByName("BLE Scene");
    REQUIRE(module != nullptr);
    CHECK(static_cast<mm::RingsEffect*>(module)->backgroundBrightness == 255);
    CHECK(tree.drivers->brightness == 137);
    char bytes[513]; mm::JsonSink state(bytes, sizeof(bytes)); envelope(state);
    mm::BleSceneCatalog::writeState(state, tree.scheduler); state.append("}");
    REQUIRE_FALSE(state.overflowed());
    CHECK(std::strlen(bytes) <= 512);
    CHECK(std::strstr(bytes, "\"background brightness\":255") != nullptr);
    const auto unchanged = tree.state();
    for (const char* controls : {R"({"background brightness":256})", R"({"background brightness":-1})", R"({"background brightness":true})"}) {
        CHECK_FALSE(tree.apply("ripple", controls).ok);
        CHECK(tree.state() == unchanged);
    }
    REQUIRE(tree.apply("ripple").ok);
    module = tree.scheduler.firstByName("BLE Scene");
    REQUIRE(module != nullptr);
    CHECK(static_cast<mm::RingsEffect*>(module)->backgroundBrightness == 0);
    CHECK(tree.drivers->brightness == 137);
}

TEST_CASE("BLE scenes: wearable solid applies the archived blue with its segment") {
    SceneTree tree;
    REQUIRE(tree.apply("nebulite-solid", R"({"red":0,"green":8,"blue":255,"white":0,"segment stop":31})").ok);
    auto* module = tree.scheduler.firstByName("BLE Scene");
    REQUIRE(module != nullptr);
    auto* solid = static_cast<mm::NebuliteSolidEffect*>(module);
    CHECK(solid->green == 8);
    CHECK(solid->blue == 255);
    CHECK(solid->segmentStop == 31);
    CHECK(tree.state().find("nebulite-solid") != std::string::npos);
}

TEST_CASE("BLE scenes: wearable blink accepts a bounded segment and rejects invalid controls") {
    SceneTree tree;
    REQUIRE(tree.apply("nebulite-blink", R"({"speed":230,"segment start":0,"segment stop":9,"secondary white":80})").ok);
    auto* module = tree.scheduler.firstByName("BLE Scene");
    REQUIRE(module != nullptr);
    auto* blink = static_cast<mm::NebuliteBlinkEffect*>(module);
    CHECK(blink->speed == 230);
    CHECK(blink->segmentStop == 9);
    CHECK(blink->secondaryWhite == 80);
    const auto unchanged = tree.state();
    CHECK_FALSE(tree.apply("nebulite-blink", R"({"frame ms":0})").ok);
    CHECK(tree.state() == unchanged);
}

TEST_CASE("BLE scenes: Firefly keeps the archived speed, density and segment") {
    SceneTree tree;
    REQUIRE(tree.apply("nebulite-firefly", R"({"speed":55,"effect intensity":255,"segment stop":9})").ok);
    auto* module = tree.scheduler.firstByName("BLE Scene"); REQUIRE(module != nullptr);
    auto* firefly = static_cast<mm::NebuliteFireflyEffect*>(module);
    CHECK(firefly->speed == 55); CHECK(firefly->intensity == 255); CHECK(firefly->segmentStop == 9);
    const auto unchanged = tree.state();
    CHECK_FALSE(tree.apply("nebulite-firefly", R"({"frame ms":0})").ok);
    CHECK(tree.state() == unchanged);
}

TEST_CASE("BLE scenes: Snowflakes keeps the archived controls and rejects invalid cadence") {
    SceneTree tree;
    REQUIRE(tree.apply("nebulite-snowflakes", R"({"speed":35,"effect intensity":197,"segment stop":31})").ok);
    auto* module = tree.scheduler.firstByName("BLE Scene"); REQUIRE(module != nullptr);
    auto* snow = static_cast<mm::NebuliteSnowflakesEffect*>(module);
    CHECK(snow->speed == 35); CHECK(snow->intensity == 197); CHECK(snow->segmentStop == 31); CHECK(snow->frameMs == 24);
    const auto unchanged = tree.state();
    CHECK_FALSE(tree.apply("nebulite-snowflakes", R"({"frame ms":0})").ok);
    CHECK(tree.state() == unchanged);
}

TEST_CASE("BLE scenes: pages fill the 512-byte response, so a scene's controls and its palette list take a handful of round trips") {
    uint16_t cursor = 0, pages = 0;
    for (; pages < 24; ++pages) {
        char bytes[513]; mm::JsonSink sink(bytes, sizeof(bytes)); envelope(sink);
        REQUIRE(mm::BleSceneCatalog::writePage(sink, cursor) == nullptr); sink.append("}");
        REQUIRE_FALSE(sink.overflowed());
        mm::json::JsonDoc page; REQUIRE(mm::json::parse(bytes, page));
        const auto* next = mm::json::member(page, page.rootNode(), "next"); REQUIRE(next != nullptr);
        if (next->type == mm::json::JsonType::Null) { ++pages; break; }
        REQUIRE(next->intValue > cursor + 1); cursor = static_cast<uint16_t>(next->intValue);
    }
    CHECK(pages <= 8);   // 35 scenes, one per page before packing

    for (const char* scene : {"ripple", "lake", "geq", "solid"}) {
        CAPTURE(scene);
        std::string names; long options = 0, expected = -1; uint16_t at = 0, count = 0;
        for (; count < 32; ++count) {
            char bytes[513]; mm::JsonSink sink(bytes, sizeof(bytes)); envelope(sink);
            REQUIRE(mm::BleSceneCatalog::writeInfo(sink, scene, at) == nullptr); sink.append("}");
            REQUIRE_FALSE(sink.overflowed());
            mm::json::JsonDoc info; REQUIRE(mm::json::parse(bytes, info));
            const auto* controls = mm::json::member(info, info.rootNode(), "controls"); REQUIRE(controls != nullptr);
            for (const auto* control = info.node(controls->firstChild); control; control = info.node(control->next)) {
                const auto* name = mm::json::member(info, control, "name"); REQUIRE(name != nullptr);
                const auto* list = mm::json::member(info, control, "options");
                if (!list) { names += name->str; names += ","; continue; }
                const auto* offset = mm::json::member(info, control, "optionOffset"); REQUIRE(offset != nullptr);
                // A list continues exactly where the previous page stopped, with each value its own index.
                if (offset->intValue == 0) { names += name->str; names += ","; options = 0; }
                REQUIRE(offset->intValue == options);
                expected = mm::json::member(info, control, "optionCount")->intValue;
                for (const auto* option = info.node(list->firstChild); option; option = info.node(option->next))
                    CHECK(mm::json::member(info, option, "value")->intValue == options++);
            }
            const auto* next = mm::json::member(info, info.rootNode(), "next"); REQUIRE(next != nullptr);
            if (next->type == mm::json::JsonType::Null) { ++count; break; }
            REQUIRE(next->intValue > at); at = static_cast<uint16_t>(next->intValue);
        }
        if (expected >= 0) CHECK(options == expected);
        CHECK(count <= 14);   // 66 to 73 pages before packing
        const bool palette = names.find("palette,") != std::string::npos;
        CHECK(palette == (std::strcmp(scene, "solid") != 0));
    }
}

TEST_CASE("BLE scenes: invalid catalog, controls, ranges and allocation failure leave the running scene intact") {
    SceneTree tree; REQUIRE(tree.apply("solid").ok); const auto original = tree.state();
    CHECK_FALSE(mm::BleSceneCatalog::apply(tree.scheduler, "solid", "wrong", "{}", true).ok);
    for (const char* controls : {R"({"made-up":1})", R"({"red":256})", R"({"red":-1})", R"({"white":4294967297})", R"({"red":true})", R"({"red":1,"red":2})", "[]"}) {
        CHECK_FALSE(tree.apply("solid", controls).ok); CHECK(tree.state() == original);
    }
    struct Restore { ~Restore() { mm::platform::setTestAllocRefusedFrom(0); } } restore;
    mm::platform::setTestAllocRefusedFrom(1);
    CHECK_FALSE(tree.apply("colortwinkles").ok);
    mm::platform::setTestAllocRefusedFrom(0); CHECK(tree.state() == original);
}

TEST_CASE("BLE scenes: the real RPC2 dispatcher applies full scenes and reads actual RGBW") {
    SceneTree tree;
    mm::BleControlService service;
    static constexpr mm::BleSceneApi api{&mm::BleSceneCatalog::writePage, &mm::BleSceneCatalog::writeInfo,
        &mm::BleSceneCatalog::validate, &mm::BleSceneCatalog::apply, &mm::BleSceneCatalog::writeState};
    service.setScheduler(&tree.scheduler); service.setScenes(&api);
    service.setEpochForTest("00000000000000000000000000000001");
    service.handle(R"({"$v":1,"$id":1,"$op":"hello","$nonce":"00000000000000000000000000000002"})", 100);
    REQUIRE(std::strstr(service.response(), "\"s\":1") != nullptr);
    uint16_t sequence = 0;
    const auto send = [&](const char* operation, const std::string& fields = "") {
        const std::string request = std::string("{\"$v\":1,\"$e\":\"00000000000000000000000000000001\",\"$s\":1,\"$id\":") +
            std::to_string(++sequence) + ",\"$op\":\"" + operation + "\"" + fields + "}";
        service.handle(request.c_str(), 100);
        CHECK(std::strlen(service.response()) <= 512);
        REQUIRE(std::strstr(service.response(), "\"ok\":true") != nullptr);
    };
    send("scene", std::string(",\"$live\":true,\"$scene\":\"solid\",\"$catalog\":\"") + mm::BleSceneCatalog::revision() + "\",\"controls\":{\"red\":11,\"green\":22,\"blue\":33,\"white\":44}");
    tree.scheduler.preparePending(); service.applyDue(100);
    send("scene-state");
    CHECK(std::strstr(service.response(), "\"id\":\"solid\"") != nullptr);
    CHECK(std::strstr(service.response(), "\"red\":11") != nullptr);
    CHECK(std::strstr(service.response(), "\"white\":44") != nullptr);
    send("scene", std::string(",\"$live\":true,\"$scene\":\"colortwinkles\",\"$catalog\":\"") + mm::BleSceneCatalog::revision() + "\",\"controls\":{\"speed\":42}");
    tree.scheduler.preparePending(); service.applyDue(100);
    send("scene-state");
    CHECK(std::strstr(service.response(), "\"id\":\"colortwinkles\"") != nullptr);
    CHECK(std::strstr(service.response(), "\"speed\":42") != nullptr);
    tree.drivers->brightness = 137;
    send("scene", std::string(",\"$live\":true,\"$scene\":\"ripple\",\"$catalog\":\"") + mm::BleSceneCatalog::revision() + "\",\"controls\":{\"background brightness\":255}");
    tree.scheduler.preparePending(); service.applyDue(100);
    send("scene-state");
    CHECK(std::strstr(service.response(), "\"id\":\"ripple\"") != nullptr);
    CHECK(std::strstr(service.response(), "\"background brightness\":255") != nullptr);
    CHECK(tree.drivers->brightness == 137);
    send("get");
    CHECK(std::strstr(service.response(), "\"pending\":null") != nullptr);
    CHECK(std::strstr(service.response(), "\"state\":\"applied\"") != nullptr);
}

TEST_CASE("BLE scenes: through the real RPC2 dispatcher, every scene's controls arrive in a handful of packed 512-byte replies whose cursor ends") {
    SceneTree tree;
    mm::BleControlService service;
    static constexpr mm::BleSceneApi api{&mm::BleSceneCatalog::writePage, &mm::BleSceneCatalog::writeInfo,
        &mm::BleSceneCatalog::validate, &mm::BleSceneCatalog::apply, &mm::BleSceneCatalog::writeState};
    service.setScheduler(&tree.scheduler); service.setScenes(&api);
    service.setEpochForTest("00000000000000000000000000000001");
    service.handle(R"({"$v":1,"$id":1,"$op":"hello","$nonce":"00000000000000000000000000000002"})", 100);
    REQUIRE(std::strstr(service.response(), "\"s\":1") != nullptr);
    uint16_t sequence = 1;
    for (uint16_t i = 0; i < mm::BleSceneCatalog::count(); ++i) {
        const char* scene = mm::product_scenes::kEntries[i].id;
        CAPTURE(scene);
        uint16_t cursor = 0, pages = 0;
        while (pages < 32) {
            ++pages;
            const std::string request = "{\"$v\":1,\"$e\":\"00000000000000000000000000000001\",\"$s\":1,\"$id\":" + std::to_string(++sequence) +
                ",\"$op\":\"scene-info\",\"$scene\":\"" + scene + "\",\"$offset\":" + std::to_string(cursor) + "}";
            service.handle(request.c_str(), 100);
            CAPTURE(service.response());
            REQUIRE(std::strlen(service.response()) <= 512);
            REQUIRE(std::strstr(service.response(), (",\"id\":" + std::to_string(sequence) + ",").c_str()) != nullptr);
            REQUIRE(std::strstr(service.response(), "\"ok\":true") != nullptr);
            mm::json::JsonDoc reply; REQUIRE(mm::json::parse(service.response(), reply));
            const auto* next = mm::json::member(reply, reply.rootNode(), "next"); REQUIRE(next != nullptr);
            if (next->type == mm::json::JsonType::Null) break;
            REQUIRE(next->intValue > cursor); cursor = static_cast<uint16_t>(next->intValue);
        }
        CHECK(pages <= 14);   // a palette scene took 74 one-part pages, past the phone's patience at a second per round trip
    }
}


TEST_CASE("BLE scenes: one scene-info request streams every page in order under its id, a retry streams them again, and a session gone ends it") {
    SceneTree tree;
    mm::BleControlService service;
    static constexpr mm::BleSceneApi api{&mm::BleSceneCatalog::writePage, &mm::BleSceneCatalog::writeInfo,
        &mm::BleSceneCatalog::validate, &mm::BleSceneCatalog::apply, &mm::BleSceneCatalog::writeState};
    service.setScheduler(&tree.scheduler); service.setScenes(&api);
    service.setEpochForTest("00000000000000000000000000000001");
    struct Recording { Recording() { mm::platform::setTestBlePublishRecorded(true); } ~Recording() { mm::platform::setTestBlePublishRecorded(false); } } recording;
    service.handle(R"({"$v":1,"$id":1,"$op":"hello","$nonce":"00000000000000000000000000000002"})", 100);
    service.tick(); mm::platform::takeTestBlePublished();
    const char* request = R"({"$v":1,"$e":"00000000000000000000000000000001","$s":1,"$id":2,"$op":"scene-info","$scene":"ripple"})";
    const auto stream = [&] {
        service.handle(request, 100);
        for (int tick = 0; tick < 40; ++tick) service.tick();
        return mm::platform::takeTestBlePublished();
    };
    const auto replies = stream();
    REQUIRE(replies.size() >= 2);
    CHECK(replies.size() <= 14);
    long cursor = 0;
    for (size_t i = 0; i < replies.size(); ++i) {
        CAPTURE(replies[i].reply);
        CHECK(replies[i].reply.size() <= 512);
        mm::json::JsonDoc page; REQUIRE(mm::json::parse(replies[i].reply.c_str(), page));
        CHECK(mm::json::member(page, page.rootNode(), "id")->intValue == 2);
        CHECK(std::strcmp(mm::json::member(page, page.rootNode(), "op")->str, "scene-info") == 0);
        const auto* controls = mm::json::member(page, page.rootNode(), "controls"); REQUIRE(controls != nullptr);
        CHECK(page.node(controls->firstChild) != nullptr);
        const auto* next = mm::json::member(page, page.rootNode(), "next"); REQUIRE(next != nullptr);
        if (i + 1 < replies.size()) { REQUIRE(next->type == mm::json::JsonType::Int); CHECK(next->intValue > cursor); cursor = next->intValue; }
        else CHECK(next->type == mm::json::JsonType::Null);
    }
    const auto again = stream();
    REQUIRE(again.size() == replies.size());
    CHECK(again.back().reply.find("\"next\":null") != std::string::npos);
    service.handle(request, 100);
    service.reconcileOwnersForTest(nullptr, 0);
    for (int tick = 0; tick < 40; ++tick) service.tick();
    CHECK(mm::platform::takeTestBlePublished().size() == 1);
}

TEST_CASE("BLE scenes: info reports the descriptors tag, FNV-1a 64 over the catalog revision and every palette name, each ended by a NUL") {
    SceneTree tree;
    uint64_t expected = 14695981039346656037ull;
    const auto fold = [&](const char* text) { for (const char* c = text;; ++c) { expected = (expected ^ static_cast<uint8_t>(*c)) * 1099511628211ull; if (!*c) break; } };
    fold(mm::BleSceneCatalog::revision());
    REQUIRE(mm::paletteCount() > mm::palettes::kCount);   // the catalog's palette scripts follow the built-ins
    for (uint8_t i = 0; i < mm::paletteCount(); ++i)
        fold(mm::LivePalettes::isLive(i) ? mm::LivePalettes::nameAt(mm::LivePalettes::sourceIndex(i)) : mm::palettes::kBuiltins[i].name);
    CHECK(mm::BleSceneCatalog::descriptors() == expected);
    mm::BleControlService service;
    static constexpr mm::BleSceneApi api{&mm::BleSceneCatalog::writePage, &mm::BleSceneCatalog::writeInfo,
        &mm::BleSceneCatalog::validate, &mm::BleSceneCatalog::apply, &mm::BleSceneCatalog::writeState, &mm::BleSceneCatalog::descriptors};
    service.setScheduler(&tree.scheduler); service.setScenes(&api);
    service.setEpochForTest("00000000000000000000000000000001");
    service.handle(R"({"$v":1,"$id":1,"$op":"hello","$nonce":"00000000000000000000000000000002"})", 100);
    service.handle(R"({"$v":1,"$e":"00000000000000000000000000000001","$s":1,"$id":2,"$op":"info"})", 100);
    char tag[32]; std::snprintf(tag, sizeof(tag), "\"descriptors\":\"%016llx\"", static_cast<unsigned long long>(expected));
    CHECK(std::strstr(service.response(), tag) != nullptr);
    CHECK(std::strlen(service.response()) <= 512);
}

TEST_CASE("BLE scan scenes: actual native control changes drive hints, not a previous scene request") {
    SceneTree tree;
    REQUIRE(tree.apply("colortwinkles", R"({"speed":248,"red":10,"green":20,"blue":30,"white":49,"palette":28})").ok);
    mm::BleAdvertisementSnapshot scan;
    mm::BleSceneCatalog::advertisement(scan, tree.scheduler);
    CHECK(scan.animation == 3); CHECK(scan.speedKnown); CHECK(scan.speed == 248);
    CHECK(scan.rgbKnown); CHECK(scan.red == 10); CHECK(scan.green == 20); CHECK(scan.blue == 30);
    CHECK(scan.whiteKnown); CHECK(scan.white == 49); CHECK(scan.palette == 28);
    auto* effect = static_cast<mm::HelioColorTwinklesEffect*>(tree.scheduler.firstByName("BLE Scene"));
    REQUIRE(effect != nullptr); effect->speed = 41;
    scan = {}; mm::BleSceneCatalog::advertisement(scan, tree.scheduler);
    CHECK(scan.speed == 41);
    effect->idleBeat = true;
    scan = {}; mm::BleSceneCatalog::advertisement(scan, tree.scheduler);
    CHECK(scan.animation == 0); CHECK_FALSE(scan.speedKnown); CHECK_FALSE(scan.rgbKnown);
    CHECK_FALSE(scan.whiteKnown); CHECK(scan.palette == 255);
}
TEST_CASE("BLE scan scenes: bpm is not misrepresented as speed and live palettes remain unknown") {
    SceneTree tree; REQUIRE(tree.apply("orbiting-lobes").ok);
    mm::BleAdvertisementSnapshot scan;
    mm::BleSceneCatalog::advertisement(scan, tree.scheduler);
    CHECK(scan.animation == 21); CHECK_FALSE(scan.speedKnown); CHECK_FALSE(scan.rgbKnown);
    CHECK(scan.palette == mm::ble_advertisement::paletteCode("Orangery"));
    tree.drivers->palette = mm::palettes::kCount;
    scan = {}; mm::BleSceneCatalog::advertisement(scan, tree.scheduler);
    CHECK(scan.animation == 21); CHECK(scan.palette == 255);
}
TEST_CASE("BLE scan scenes: a changed non-exposed solid baseline produces custom hints") {
    SceneTree tree; REQUIRE(tree.apply("solid", R"({"red":255,"green":0,"blue":0,"white":99})").ok);
    mm::BleAdvertisementSnapshot scan;
    mm::BleSceneCatalog::advertisement(scan, tree.scheduler);
    CHECK(scan.animation == 1); CHECK(scan.rgbKnown); CHECK(scan.whiteKnown); CHECK_FALSE(scan.speedKnown);
    auto* solid = static_cast<mm::SolidEffect*>(tree.scheduler.firstByName("BLE Scene"));
    REQUIRE(solid != nullptr); solid->colorMode = mm::SolidEffect::kColorPalette;
    scan = {}; mm::BleSceneCatalog::advertisement(scan, tree.scheduler);
    CHECK(scan.animation == 0); CHECK_FALSE(scan.rgbKnown); CHECK_FALSE(scan.whiteKnown);
}

TEST_CASE("The NEBULITE boot effect, a Rainbow at its defaults, reads back as the catalog's Rainbow scene") {
    SceneTree tree;
    auto* rainbow = mm::ModuleFactory::create("NebuliteRainbowEffect");
    REQUIRE(rainbow != nullptr);
    rainbow->setName("Rainbow");
    tree.layer->addChild(rainbow);
    tree.layer->rebuildControls();
    tree.scheduler.setup();
    CHECK(tree.state().find("\"id\":\"nebulite-rainbow\"") != std::string::npos);
}
