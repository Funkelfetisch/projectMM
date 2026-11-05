/// @module ProductPresets
#include "doctest.h"
#include "helio/ProductPresets.h"
#include "module_types.h"
#include "light/drivers/Drivers.h"
#include "light/effects/HelioWledPresetsEffect.h"
#include "light/effects/SolidEffect.h"
#include "light/effects/RingsEffect.h"
#include "light/effects/LavaLampEffect.h"
#include "light/effects/DistortionWavesEffect.h"
#include "light/effects/PlasmaEffect.h"
#include "light/effects/MetaballsEffect.h"
#include "light/layouts/GridLayout.h"
#include "light/layouts/Layouts.h"
#include "light/layers/Effects.h"
#include "light/layers/Layer.h"

#include <cstring>
#include <filesystem>
#include <string>

namespace {
struct ProductTree {
    std::string previousRoot = mm::platform::fsRootPath();
    std::filesystem::path root;
    mm::Layouts layouts;
    mm::GridLayout grid;
    mm::Scheduler scheduler;
    mm::Effects* effects = new mm::Effects();
    mm::Layer* layer = new mm::Layer();
    mm::Drivers* drivers = new mm::Drivers();
    explicit ProductTree(const char* layerName = "Render plane") {
        static unsigned sequence = 0;
        root = std::filesystem::temp_directory_path() / ("mm_product_presets_" + std::to_string(++sequence));
        std::filesystem::remove_all(root);
        mm::platform::fsSetRoot(root.string().c_str());
        mm::registerModuleTypes();
#define REGISTER_PRODUCT_TYPE(Type) mm::ModuleFactory::registerType<mm::Type>(#Type)
        REGISTER_PRODUCT_TYPE(Layer); REGISTER_PRODUCT_TYPE(Drivers);
        REGISTER_PRODUCT_TYPE(SolidEffect); REGISTER_PRODUCT_TYPE(RingsEffect);
        REGISTER_PRODUCT_TYPE(LavaLampEffect); REGISTER_PRODUCT_TYPE(DistortionWavesEffect);
        REGISTER_PRODUCT_TYPE(PlasmaEffect); REGISTER_PRODUCT_TYPE(MetaballsEffect);
        REGISTER_PRODUCT_TYPE(HelioCandleMultiEffect); REGISTER_PRODUCT_TYPE(HelioColorTwinklesEffect);
        REGISTER_PRODUCT_TYPE(HelioFreqMapAmberEffect); REGISTER_PRODUCT_TYPE(HelioLakeEffect);
        REGISTER_PRODUCT_TYPE(HelioLoadingEffect); REGISTER_PRODUCT_TYPE(HelioPinkPaletteEffect);
        REGISTER_PRODUCT_TYPE(HelioPhasedEffect); REGISTER_PRODUCT_TYPE(HelioRunningEffect);
        REGISTER_PRODUCT_TYPE(HelioStarburstEffect); REGISTER_PRODUCT_TYPE(HelioGravCenterEffect);
        REGISTER_PRODUCT_TYPE(HelioJugglesEffect); REGISTER_PRODUCT_TYPE(HelioMultiCometEffect);
        REGISTER_PRODUCT_TYPE(HelioNoiseMeterEffect); REGISTER_PRODUCT_TYPE(HelioPlasmoidEffect);
        REGISTER_PRODUCT_TYPE(HelioFreqMapWideEffect); REGISTER_PRODUCT_TYPE(HelioGEQEffect);
        REGISTER_PRODUCT_TYPE(HelioGravFreqEffect); REGISTER_PRODUCT_TYPE(HelioFreqWaveEffect);
#undef REGISTER_PRODUCT_TYPE
        grid.width = 64; grid.height = 1; grid.depth = 1;
        layouts.addChild(&grid); layouts.applyState();
        effects->setName("Effects"); effects->setTypeName("Effects");
        layer->setName(layerName); layer->setTypeName("Layer");
        layer->setLayouts(&layouts); layer->setChannelsPerLight(4); layer->setWorkingMemoryReserve(40 * 1024);
        drivers->setName("Drivers"); drivers->setTypeName("Drivers");
        effects->addChild(layer); effects->setLayouts(&layouts); effects->defineControls(); drivers->defineControls();
        scheduler.addModule(effects); scheduler.addModule(drivers);
    }
    ~ProductTree() {
        scheduler.release();
        mm::platform::setTestFreeHeap(0); mm::platform::setTestFreeInternalHeap(0);
        mm::platform::fsSetRoot(previousRoot.c_str());
        std::filesystem::remove_all(root);
    }
    std::string state() {
        mm::JsonSink sink; sink.append("{"); mm::writeStateMember(sink, *effects); sink.append("}");
        REQUIRE_FALSE(sink.overflowed()); return {sink.data(), sink.size()};
    }
    static void removePresets() {
        for (const auto& file : std::filesystem::directory_iterator(std::filesystem::path(mm::platform::fsRootPath()) / ".config/presets"))
            if (file.path().extension() == ".json") std::filesystem::remove(file.path());
    }
};

const mm::json::JsonNode* requiredMember(const mm::json::JsonDoc& doc, const mm::json::JsonNode* object, const char* name) {
    const auto* value = mm::json::member(doc, object, name);
    REQUIRE(value != nullptr);
    return value;
}

std::string paletteLabel(uint8_t index) {
    char label[96]; mm::JsonSink sink(label, sizeof(label));
    sink.requestName(index); mm::paletteOptions(sink);
    REQUIRE_FALSE(sink.overflowed()); REQUIRE(label[0] != '\0'); return label;
}
}

TEST_CASE("Product presets generate every complete editable factory document without mutating the runtime") {
    ProductTree tree;
    const auto before = tree.state();
    const auto revision = tree.scheduler.preparationRevision();
    const bool pending = tree.scheduler.preparationPending();
    mm::JsonSink index;
    mm::ProductPresets::writeIndex(index);
    mm::json::JsonDoc listed; REQUIRE(mm::json::parse(index.data(), listed));
    CHECK(std::strcmp(requiredMember(listed, listed.rootNode(), "catalog")->str, mm::BleSceneCatalog::revision()) == 0);
    unsigned count = 0;
    for (const auto* item = listed.node(requiredMember(listed, listed.rootNode(), "items")->firstChild); item; item = listed.node(item->next)) {
        const char* id = requiredMember(listed, item, "id")->str;
        mm::JsonSink output; REQUIRE(mm::ProductPresets::writeDocument(output, tree.scheduler, id) == nullptr);
        REQUIRE_FALSE(output.overflowed());
        mm::json::JsonDoc doc; REQUIRE(mm::json::parse(output.data(), doc));
        REQUIRE(mm::PresetStore::isLook(doc));
        const auto* origin = requiredMember(doc, doc.rootNode(), "$origin");
        CHECK(std::strcmp(requiredMember(doc, origin, "scene")->str, id) == 0);
        const auto* schema = requiredMember(doc, doc.rootNode(), "$uiSchema");
        const auto* path = requiredMember(doc, schema, "effectPath");
        CHECK(std::strcmp(doc.nodes[doc.nodes[path->firstChild].next].str, "Render plane") == 0);
        for (const auto* control = doc.node(requiredMember(doc, schema, "controls")->firstChild); control; control = doc.node(control->next)) {
            requiredMember(doc, control, "name"); requiredMember(doc, control, "type");
            requiredMember(doc, control, "min"); requiredMember(doc, control, "max"); requiredMember(doc, control, "default");
            CHECK(mm::json::member(doc, control, "options") == nullptr);
        }
        CHECK(tree.state() == before);
        CHECK(tree.scheduler.preparationRevision() == revision);
        CHECK(tree.scheduler.preparationPending() == pending);
        ++count;
    }
    CHECK(count == mm::BleSceneCatalog::count());
    mm::JsonSink invalid; CHECK(mm::ProductPresets::writeDocument(invalid, tree.scheduler, "unknown") != nullptr);
    CHECK(invalid.size() == 0);
}

TEST_CASE("Product preset application preserves the actual RGBW layer and its working memory reserve") {
    ProductTree tree;
    mm::JsonSink document;
    REQUIRE(mm::ProductPresets::writeDocument(document, tree.scheduler, "solid") == nullptr);
    REQUIRE(mm::applyStateDocument(tree.scheduler, document.data(), mm::StateSource::Live).ok);
    CHECK(tree.scheduler.firstByName("Render plane") == tree.layer);
    CHECK(tree.layer->channelsPerLight() == 4);
    auto* effect = static_cast<mm::SolidEffect*>(tree.scheduler.firstByName("BLE Scene"));
    REQUIRE(effect != nullptr);
    CHECK(effect->red == 255); CHECK(effect->green == 133); CHECK(effect->white == 0);
    // A fresh output below the product reserve cannot allocate a raster, even above the generic reserve; retaining an existing admitted scene is tested separately.
    mm::platform::setTestFreeHeap(39 * 1024); mm::platform::setTestFreeInternalHeap(39 * 1024);
    tree.layer->prepare();
    CHECK(tree.layer->width() == 0);
    CHECK(tree.layer->buffer().data() == nullptr);
    CHECK(tree.layer->dynamicBytes() == 0);
    CHECK(tree.layer->severity() == mm::MoonModule::Severity::Error);
    REQUIRE(tree.layer->status() != nullptr);
    CAPTURE(std::string(tree.layer->status()));
    CAPTURE(tree.layer->width());
    CHECK(std::strstr(tree.layer->status(), "buffer allocation failed") != nullptr);
    mm::platform::setTestFreeHeap(0); mm::platform::setTestFreeInternalHeap(0);
    tree.layer->prepare();
    REQUIRE(tree.layer->buffer().data() != nullptr);
    CHECK(tree.layer->width() == 64);
    CHECK(tree.layer->channelsPerLight() == 4);
}

TEST_CASE("Product documents disable competing code-wired layers and effects through the shared scene engine") {
    ProductTree tree("Layer");
    auto* other = new mm::Layer(); other->setName("Other"); other->setTypeName("Layer"); other->markWiredByCode();
    other->defineControls(); tree.effects->addChild(other);
    auto* wired = new mm::SolidEffect(); wired->setName("Wired"); wired->setTypeName("SolidEffect"); wired->markWiredByCode();
    wired->defineControls(); tree.layer->addChild(wired);
    mm::JsonSink document; REQUIRE(mm::ProductPresets::writeDocument(document, tree.scheduler, "colortwinkles") == nullptr);
    CHECK(other->enabled()); CHECK(wired->enabled());
    REQUIRE(mm::applyStateDocument(tree.scheduler, document.data(), mm::StateSource::Live).ok);
    CHECK_FALSE(other->enabled()); CHECK_FALSE(wired->enabled());
    auto* scene = static_cast<mm::HelioColorTwinklesEffect*>(tree.scheduler.firstByName("BLE Scene"));
    REQUIRE(scene != nullptr); CHECK(scene->speed == 128); CHECK(scene->white == 255);
}

TEST_CASE("Product preset seeding preserves existing customer content and the marker allows deleting every look") {
    ProductTree tree;
    const char* custom = R"({"Effects":{"enabled":false},"$owner":"customer"})";
    REQUIRE(mm::PresetStore::put("Solid RGBW", custom, mm::PresetStore::Mode::Create));
    const auto before = tree.state();
    REQUIRE(mm::ProductPresets::initialize(tree.scheduler) == nullptr);
    REQUIRE(mm::platform::fsExists(mm::ProductPresets::kMarker));
    mm::PresetStore::File preserved;
    REQUIRE(mm::PresetStore::read("Solid RGBW", preserved));
    CHECK(std::strcmp(preserved.text, custom) == 0);
    for (const auto& entry : mm::product_scenes::kEntries) {
        char name[mm::PresetStore::kMaxNameLen];
        REQUIRE(mm::ProductPresets::fileName(entry.name, name));
        char path[80]; mm::PresetStore::pathFor(name, path, sizeof(path));
        CHECK(mm::platform::fsExists(path));
    }
    CHECK(mm::platform::fsExists("/.config/presets/Music - Rainbow.json"));
    CHECK(mm::platform::fsExists("/.config/presets/Music - Noisefire.json"));
    CHECK(tree.state() == before);
    ProductTree::removePresets();
    REQUIRE(mm::ProductPresets::initialize(tree.scheduler) == nullptr);
    for (const auto& file : std::filesystem::directory_iterator(tree.root / ".config/presets"))
        CHECK(file.path().extension() != ".json");
}

TEST_CASE("A factory scene the catalog gains later is offered once, and a deleted look stays deleted") {
    ProductTree tree;
    REQUIRE(mm::ProductPresets::initialize(tree.scheduler) == nullptr);
    char first[mm::PresetStore::kMaxNameLen], last[mm::PresetStore::kMaxNameLen];
    REQUIRE(mm::ProductPresets::fileName(mm::product_scenes::kEntries[0].name, first));
    const auto& newest = mm::product_scenes::kEntries[std::size(mm::product_scenes::kEntries) - 1];
    REQUIRE(mm::ProductPresets::fileName(newest.name, last));
    // The board was seeded before the catalog had its newest scene, and its owner deleted the first look since.
    std::string earlier;
    for (const auto& entry : mm::product_scenes::kEntries)
        if (&entry != &newest) earlier.append(entry.id).append("\n");
    REQUIRE(mm::platform::fsWriteAtomic(mm::ProductPresets::kMarker, earlier.data(), earlier.size()));
    REQUIRE(mm::PresetStore::erase(first, nullptr, false));
    REQUIRE(mm::PresetStore::erase(last, nullptr, false));
    REQUIRE(mm::ProductPresets::initialize(tree.scheduler) == nullptr);
    mm::PresetStore::File file;
    CHECK(mm::PresetStore::read(last, file));
    CHECK_FALSE(mm::PresetStore::read(first, file));
    // Offered once: deleting the new look too keeps it deleted.
    REQUIRE(mm::PresetStore::erase(last, nullptr, false));
    REQUIRE(mm::ProductPresets::initialize(tree.scheduler) == nullptr);
    CHECK_FALSE(mm::PresetStore::read(last, file));
}

TEST_CASE("A board marked by an earlier build gets the factory looks it lacks, and keeps the ones it has") {
    ProductTree tree;
    const char* custom = R"({"Effects":{"enabled":false},"$owner":"customer"})";
    char kept[mm::PresetStore::kMaxNameLen];
    REQUIRE(mm::ProductPresets::fileName(mm::product_scenes::kEntries[0].name, kept));
    REQUIRE(mm::PresetStore::put(kept, custom, mm::PresetStore::Mode::Create));
    // The earlier marker held the catalog revision it seeded, not the scenes.
    REQUIRE(mm::platform::fsWriteAtomic(mm::ProductPresets::kMarker, "0123456789ab", 12));
    REQUIRE(mm::ProductPresets::initialize(tree.scheduler) == nullptr);
    mm::PresetStore::File file;
    REQUIRE(mm::PresetStore::read(kept, file));
    CHECK(std::strcmp(file.text, custom) == 0);
    for (const auto& entry : mm::product_scenes::kEntries) {
        char name[mm::PresetStore::kMaxNameLen];
        REQUIRE(mm::ProductPresets::fileName(entry.name, name));
        CHECK(mm::PresetStore::read(name, file));
    }
}

TEST_CASE("Product preset seeding leaves no completion marker after a partial failure and retries only missing files") {
    ProductTree tree;
    const char* custom = R"({"Effects":{"enabled":false}})";
    for (unsigned i = 0; i < mm::PresetStore::kMaxPresets - 1; ++i) {
        const auto name = "Customer " + std::to_string(i);
        REQUIRE(mm::PresetStore::put(name.c_str(), custom, mm::PresetStore::Mode::Create));
    }
    CHECK(mm::ProductPresets::initialize(tree.scheduler) != nullptr);
    CHECK_FALSE(mm::platform::fsExists(mm::ProductPresets::kMarker));
    mm::PresetStore::File first;
    REQUIRE(mm::PresetStore::read("Solid RGBW", first));
    REQUIRE(mm::PresetStore::put("Solid RGBW", custom, mm::PresetStore::Mode::Overwrite));
    for (unsigned i = 0; i < mm::PresetStore::kMaxPresets - 1; ++i) {
        const auto name = "Customer " + std::to_string(i);
        REQUIRE(mm::PresetStore::erase(name.c_str(), nullptr, false));
    }
    REQUIRE(mm::ProductPresets::initialize(tree.scheduler) == nullptr);
    CHECK(mm::platform::fsExists(mm::ProductPresets::kMarker));
    mm::PresetStore::File preserved; REQUIRE(mm::PresetStore::read("Solid RGBW", preserved));
    CHECK(std::strcmp(preserved.text, custom) == 0);
}

TEST_CASE("Product first seed preserves unique tuned disabled instances while reset templates retain factory defaults") {
    ProductTree tree;
    auto* phase = new mm::HelioPhasedEffect();
    phase->setName("My phase"); phase->setTypeName("HelioPhasedEffect");
    phase->speed = 15; phase->red = 12; phase->green = 34; phase->blue = 56;
    phase->white = 78; phase->output = 90; phase->paletteIndex = 23;
    phase->defineControls(); phase->setEnabled(false); tree.layer->addChild(phase);
    auto* rings = new mm::RingsEffect();
    rings->setName("My ripples"); rings->setTypeName("RingsEffect");
    rings->speed = 1; rings->count = 3; rings->thickness = 7; rings->hue_shift = 29;
    rings->defineControls(); rings->setEnabled(false); tree.layer->addChild(rings);
    tree.drivers->palette = 14;
    const auto before = tree.state();
    REQUIRE(mm::ProductPresets::initialize(tree.scheduler) == nullptr);
    CHECK(tree.state() == before); CHECK(tree.drivers->palette == 14);
    mm::PresetStore::File savedPhase;
    REQUIRE(mm::PresetStore::read("Phase Shimmer", savedPhase));
    mm::json::JsonDoc doc; REQUIRE(mm::json::parse(savedPhase.text, doc));
    const auto* effect = requiredMember(doc, requiredMember(doc, requiredMember(doc, doc.rootNode(), "Effects"), "Render plane"), "BLE Scene");
    CHECK(requiredMember(doc, effect, "speed")->intValue == 15);
    CHECK(requiredMember(doc, effect, "red")->intValue == 12); CHECK(requiredMember(doc, effect, "green")->intValue == 34);
    CHECK(requiredMember(doc, effect, "blue")->intValue == 56); CHECK(requiredMember(doc, effect, "white")->intValue == 78);
    CHECK(std::string(requiredMember(doc, effect, "palette")->str) == paletteLabel(23)); CHECK(requiredMember(doc, effect, "output")->intValue == 90);
    CHECK(std::strcmp(requiredMember(doc, requiredMember(doc, doc.rootNode(), "$origin"), "seedSource")->str, "existing-instance") == 0);
    mm::JsonSink reset;
    REQUIRE(mm::ProductPresets::writeDocument(reset, tree.scheduler, "phased") == nullptr);
    mm::json::JsonDoc defaults; REQUIRE(mm::json::parse(reset.data(), defaults));
    const auto* defaultEffect = requiredMember(defaults, requiredMember(defaults, requiredMember(defaults, defaults.rootNode(), "Effects"), "Render plane"), "BLE Scene");
    CHECK(requiredMember(defaults, defaultEffect, "speed")->intValue == 128);
    CHECK(std::string(requiredMember(defaults, defaultEffect, "palette")->str) == paletteLabel(53));
    REQUIRE(mm::applyStateDocument(tree.scheduler, savedPhase.text, mm::StateSource::Live).ok);
    auto* applied = static_cast<mm::HelioPhasedEffect*>(tree.scheduler.firstByName("BLE Scene"));
    REQUIRE(applied != nullptr); CHECK(applied->speed == 15); CHECK(applied->white == 78);
    CHECK(applied->red == 12); CHECK(applied->green == 34); CHECK(applied->blue == 56);
    CHECK(applied->paletteIndex == 23); CHECK(applied->output == 90);
    mm::PresetStore::File savedRings; REQUIRE(mm::PresetStore::read("Ripple", savedRings));
    mm::json::JsonDoc ripple; REQUIRE(mm::json::parse(savedRings.text, ripple));
    CHECK(std::string(requiredMember(ripple, requiredMember(ripple, ripple.rootNode(), "Drivers"), "palette")->str) == paletteLabel(0));
    REQUIRE(mm::applyStateDocument(tree.scheduler, savedRings.text, mm::StateSource::Live).ok);
    auto* appliedRings = static_cast<mm::RingsEffect*>(tree.scheduler.firstByName("BLE Scene"));
    REQUIRE(appliedRings != nullptr); CHECK(appliedRings->speed == 1); CHECK(appliedRings->count == 3);
    CHECK(appliedRings->thickness == 7); CHECK(appliedRings->hue_shift == 29);
    CHECK(tree.drivers->palette == 0);
    CHECK(tree.scheduler.firstByName("Render plane") == tree.layer); CHECK(tree.layer->channelsPerLight() == 4);
}

TEST_CASE("Product seed captures global palette only for the uniquely matching active native effect") {
    ProductTree tree;
    auto* rings = new mm::RingsEffect(); rings->setName("My ripples"); rings->setTypeName("RingsEffect");
    rings->speed = 1; rings->defineControls(); tree.layer->addChild(rings);
    tree.drivers->palette = 36;
    const auto before = tree.state();
    REQUIRE(mm::ProductPresets::initialize(tree.scheduler) == nullptr);
    CHECK(tree.state() == before); CHECK(tree.drivers->palette == 36);
    mm::PresetStore::File saved; REQUIRE(mm::PresetStore::read("Ripple", saved));
    mm::json::JsonDoc doc; REQUIRE(mm::json::parse(saved.text, doc));
    CHECK(std::string(requiredMember(doc, requiredMember(doc, doc.rootNode(), "Drivers"), "palette")->str) == paletteLabel(36));
    mm::PresetStore::File other; REQUIRE(mm::PresetStore::read("Liquid Mirror", other));
    mm::json::JsonDoc otherDoc; REQUIRE(mm::json::parse(other.text, otherDoc));
    CHECK(std::string(requiredMember(otherDoc, requiredMember(otherDoc, otherDoc.rootNode(), "Drivers"), "palette")->str) == paletteLabel(53));
    tree.drivers->palette = 0;
    REQUIRE(mm::applyStateDocument(tree.scheduler, saved.text, mm::StateSource::Live).ok);
    CHECK(tree.drivers->palette == 36);
}

TEST_CASE("Product seed documents ambiguous existing types and keeps the factory rather than guessing a tuned instance") {
    ProductTree tree;
    for (unsigned i = 0; i < 2; ++i) {
        auto* phase = new mm::HelioPhasedEffect();
        phase->setName(i ? "Second phase" : "First phase"); phase->setTypeName("HelioPhasedEffect");
        phase->speed = i ? 99 : 15; phase->defineControls(); phase->setEnabled(false); tree.layer->addChild(phase);
    }
    const auto before = tree.state();
    REQUIRE(mm::ProductPresets::initialize(tree.scheduler) == nullptr);
    CHECK(tree.state() == before);
    mm::PresetStore::File saved; REQUIRE(mm::PresetStore::read("Phase Shimmer", saved));
    mm::json::JsonDoc doc; REQUIRE(mm::json::parse(saved.text, doc));
    const auto* effect = requiredMember(doc, requiredMember(doc, requiredMember(doc, doc.rootNode(), "Effects"), "Render plane"), "BLE Scene");
    CHECK(requiredMember(doc, effect, "speed")->intValue == 128);
    CHECK(std::strcmp(requiredMember(doc, requiredMember(doc, doc.rootNode(), "$origin"), "seedSource")->str, "factory-ambiguous") == 0);
}

TEST_CASE("Product documents persist option names while RPC documents and UI descriptor defaults remain numeric") {
    ProductTree tree;
    mm::JsonSink product;
    REQUIRE(mm::ProductPresets::writeDocument(product, tree.scheduler, "solid") == nullptr);
    mm::json::JsonDoc file; REQUIRE(mm::json::parse(product.data(), file));
    const auto* layer = requiredMember(file, requiredMember(file, file.rootNode(), "Effects"), "Render plane");
    CHECK(std::string(requiredMember(file, layer, "blendMode")->str) == "additive");
    const auto* effect = requiredMember(file, layer, "BLE Scene");
    CHECK(std::string(requiredMember(file, effect, "colorMode")->str) == "RGB(W)");
    REQUIRE(mm::applyStateDocument(tree.scheduler, product.data(), mm::StateSource::Live).ok);
    CHECK(tree.layer->blendMode == mm::Layer::kBlendAdditive);
    auto* solid = static_cast<mm::SolidEffect*>(tree.scheduler.firstByName("BLE Scene"));
    REQUIRE(solid != nullptr); CHECK(solid->colorMode == mm::SolidEffect::kColorFlat);

    mm::JsonSink templateDoc;
    REQUIRE(mm::ProductPresets::writeDocument(templateDoc, tree.scheduler, "colortwinkles") == nullptr);
    mm::json::JsonDoc twinkles; REQUIRE(mm::json::parse(templateDoc.data(), twinkles));
    const auto* twinkleEffect = requiredMember(twinkles, requiredMember(twinkles, requiredMember(twinkles, twinkles.rootNode(), "Effects"), "Render plane"), "BLE Scene");
    const auto* palette = requiredMember(twinkles, twinkleEffect, "palette");
    REQUIRE(palette->type == mm::json::JsonType::String); CHECK(std::string(palette->str) == paletteLabel(28));
    const auto* schema = requiredMember(twinkles, twinkles.rootNode(), "$uiSchema");
    bool numericDefault = false;
    for (const auto* control = twinkles.node(requiredMember(twinkles, schema, "controls")->firstChild); control; control = twinkles.node(control->next))
        if (!std::strcmp(requiredMember(twinkles, control, "name")->str, "palette")) {
            CHECK(requiredMember(twinkles, control, "default")->type == mm::json::JsonType::Int);
            CHECK(requiredMember(twinkles, control, "default")->intValue == 28); numericDefault = true;
        }
    CHECK(numericDefault);
    REQUIRE(mm::applyStateDocument(tree.scheduler, templateDoc.data(), mm::StateSource::Live).ok);
    auto* applied = static_cast<mm::HelioColorTwinklesEffect*>(tree.scheduler.firstByName("BLE Scene"));
    REQUIRE(applied != nullptr); CHECK(applied->paletteIndex == 28);

    mm::JsonSink rpc;
    REQUIRE(mm::BleSceneCatalog::writeDocument(tree.scheduler, "colortwinkles", mm::BleSceneCatalog::revision(), "{}", rpc).ok);
    mm::json::JsonDoc wire; REQUIRE(mm::json::parse(rpc.data(), wire));
    const auto* rpcLayer = requiredMember(wire, requiredMember(wire, wire.rootNode(), "Effects"), "Render plane");
    CHECK(requiredMember(wire, rpcLayer, "blendMode")->type == mm::json::JsonType::Int);
    CHECK(requiredMember(wire, requiredMember(wire, rpcLayer, "BLE Scene"), "palette")->intValue == 28);
}
