/// @module PresetStore
#include "doctest.h"
#include "core/util/PresetStore.h"
#include "core/util/PresetTemplates.h"
#include "core/system/ControlModule.h"
#include "core/system/HttpServerModule.h"
#include "core/util/build_info.h"
#include "ui/ui_embedded.h"
#include "http_loopback.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace {
struct PresetEffect : mm::MoonModule {
    uint8_t speed = 7;
    uint8_t frames = 0;
    uint8_t phase = 0;
    bool refuseAllocationsAfterPrepare = false;
    mm::ModuleRole role() const MM_NONBLOCKING override { return mm::ModuleRole::Effect; }
    void defineControls() override {
        controls_.addControl("speed", speed, 0, 255);
        controls_.addControl("frames", frames, 0, 255);
        controls_.setReadOnly(controls_.count() - 1, true);
        controls_.addControl("phase", phase, 0, 255);
        controls_.setLive(controls_.count() - 1);
        controls_.addButton("reset");
    }
    void prepare() override {
        if (refuseAllocationsAfterPrepare) mm::platform::setTestAllocRefusedFrom(1);
    }
};
struct PresetLayer : mm::MoonModule {
    mm::ModuleRole role() const MM_NONBLOCKING override { return mm::ModuleRole::Layer; }
    const char* acceptsChildRoles() const override { return "effect,modifier"; }
};
struct PresetEffects : mm::MoonModule {
    const char* acceptsChildRoles() const override { return "layer"; }
};
struct PresetDrivers : mm::MoonModule {
    uint8_t pin = 35;
    uint8_t palette = 1;
    char paletteScript[32] = {};
    mm::ModuleRole role() const MM_NONBLOCKING override { return mm::ModuleRole::Driver; }
    void defineControls() override {
        controls_.addControl("pin", pin, 0, 48);
        controls_.addControl("palette", palette, 0, 255);
        controls_.addText("paletteScript", paletteScript, sizeof(paletteScript));
    }
};

constexpr const char* kLook = R"({"$slot":1,"$origin":{"scene":"sparkles","catalog":"test"},"$uiSchema":{"effectType":"PresetStoreEffect","controls":[]},"Effects":{"$patch":"replace","Layer":{"type":"PresetStoreLayer","$patch":"replace","Scene":{"type":"PresetStoreEffect","speed":61}}}})";

struct PresetDevice {
    std::string previousRoot = mm::platform::fsRootPath();
    std::string root;
    mm::Scheduler scheduler;
    PresetEffects* effects = new PresetEffects();
    PresetLayer* layer = new PresetLayer();
    PresetEffect* effect = new PresetEffect();
    PresetDrivers* drivers = new PresetDrivers();
    mm::ControlModule* control = new mm::ControlModule();
    mm::HttpServerModule http;

    PresetDevice() {
        static unsigned sequence = 0;
        root = "/tmp/mm_native_preset_store_" + std::to_string(++sequence);
        std::filesystem::remove_all(root);
        mm::platform::fsSetRoot(root.c_str());
        mm::platform::fsMount();
        mm::ModuleFactory::registerType<PresetLayer>("PresetStoreLayer");
        mm::ModuleFactory::registerType<PresetEffect>("PresetStoreEffect");
        mm::ModuleFactory::registerType<PresetDrivers>("PresetStoreDrivers");
        effects->setName("Effects");
        layer->setName("Layer"); layer->setTypeName("PresetStoreLayer");
        effect->setName("Scene"); effect->setTypeName("PresetStoreEffect");
        drivers->setName("Drivers");
        control->setName("Control");
        layer->addChild(effect); effects->addChild(layer);
        scheduler.addModule(effects); scheduler.addModule(drivers); scheduler.addModule(control);
        scheduler.setup();
        http.setScheduler(&scheduler);
        http.port = 18119;
        http.setup();
    }
    ~PresetDevice() {
        mm::platform::setTestAllocRefusedFrom(0);
        http.release(); scheduler.release();
        std::filesystem::remove_all(root);
        mm::platform::fsSetRoot(previousRoot.c_str());
    }
    std::string request(const char* method, const std::string& path, const std::string& body = {}, const std::string& headers = {}) {
        mm::platform::TcpConnection client;
        REQUIRE(http_loopback::connectAndAccept(http, client, 18119));
        const std::string message = std::string(method) + " " + path + " HTTP/1.1\r\nHost: test\r\n" + headers +
            "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
        REQUIRE(client.write(reinterpret_cast<const uint8_t*>(message.data()), message.size()));
        const auto reply = http_loopback::readUntilClosed(http, client);
        client.close(); return reply;
    }
};

std::string tagHeader(const char* etag) { return std::string("If-Match: \"") + etag + "\"\r\n"; }
bool status(const std::string& reply, int expected) { return reply.starts_with("HTTP/1.1 " + std::to_string(expected) + " "); }

struct PresetFileObserver : mm::MoonModule {
    unsigned calls = 0;
    std::string path;
    void onFileChanged(const char* changed) override { ++calls; path = changed; }
};
}

TEST_CASE("Native preset content-only HTTP autosave announces its file and revision without rebuilding schema") {
    PresetDevice device;
    REQUIRE(status(device.request("PUT", "/api/presets/Look", kLook, "If-None-Match: *\r\n"), 201));
    REQUIRE(device.control->listRowCount() == 1);
    SUBCASE("the control surface is enabled") {}
    SUBCASE("a disabled control surface still refreshes its preset projection") { device.control->setEnabled(false); }
    mm::PresetStore::File original;
    REQUIRE(mm::PresetStore::read("Look", original));
    const auto revision = device.control->presetsRevision();
    mm::JsonSink row;
    device.control->writeListRow(row, 0);
    const std::string before(row.data(), row.size());
    auto* observer = new PresetFileObserver;
    observer->setName("Observer");
    device.scheduler.addModule(observer);
    static unsigned* observedSchema = nullptr;
    unsigned schemaEvents = 0;
    struct Hook {
        explicit Hook(unsigned& count) {
            observedSchema = &count;
            mm::MoonModule::setSchemaChangedHook([] { ++*observedSchema; });
        }
        ~Hook() { mm::MoonModule::setSchemaChangedHook(nullptr); observedSchema = nullptr; }
    } hook(schemaEvents);
    std::string changed(kLook);
    changed.replace(changed.find("\"speed\":61"), std::strlen("\"speed\":61"), "\"speed\":62");

    const auto reply = device.request("PUT", "/api/presets/Look", changed, tagHeader(original.etag));

    REQUIRE(status(reply, 200));
    CHECK(observer->calls == 1);
    CHECK(observer->path == "/.config/presets/Look.json");
    CHECK(device.control->presetsRevision() > revision);
    CHECK(schemaEvents == 0);
    mm::JsonSink currentRow;
    device.control->writeListRow(currentRow, 0);
    CHECK(std::string(currentRow.data(), currentRow.size()) == before);
    mm::PresetStore::File saved;
    REQUIRE(mm::PresetStore::read("Look", saved));
    CHECK(std::strcmp(saved.text, changed.c_str()) == 0);
    CHECK(std::strcmp(saved.etag, original.etag) != 0);
    CHECK(device.effect->speed == 7); // file save does not apply the look
}

TEST_CASE("Native look stores and applies with a historical preview without applying its metadata") {
    PresetDevice device;
    std::string document(kLook);
    document.pop_back();
    document += R"(,"$recordedPreview":{"version":1,"encoding":"rgb8-base64","ledCount":31,"frameMs":100,"data":")";
    document += std::string(6200, 'A'); // 50 frames of 31 RGB bytes encoded as base64
    document += R"(","forLook":"{}"}})";
    REQUIRE(status(device.request("PUT", "/api/presets/Archive", document, "If-None-Match: *\r\n"), 201));
    mm::PresetStore::File stored;
    REQUIRE(mm::PresetStore::read("Archive", stored));
    CHECK(std::string(stored.text, stored.size) == document);
    const auto index = device.request("GET", "/api/presets");
    CHECK(index.find("\"kind\":\"look\"") != std::string::npos);
    CHECK(index.find(std::string(256, 'A')) == std::string::npos);
    CHECK(device.effect->speed == 7);
    REQUIRE(status(device.request("POST", "/api/presets/Archive/apply", "{}", tagHeader(stored.etag)), 200));
    CHECK(device.effect->speed == 61);
    CHECK(device.drivers->pin == 35);
}

TEST_CASE("Native preset storage preserves documents and rejects stale writes and deletes") {
    PresetDevice device;
    const auto created = mm::PresetStore::put("Sparkles", kLook, mm::PresetStore::Mode::Create);
    REQUIRE(created.status == 201);
    CHECK(created.created);
    CHECK(std::strlen(created.etag) == 64);
    CHECK(mm::PresetStore::put("Sparkles", kLook, mm::PresetStore::Mode::Create).status == 412);
    CHECK(mm::PresetStore::put("Sparkles", kLook, mm::PresetStore::Mode::Match).status == 428);
    mm::PresetStore::File file;
    REQUIRE(mm::PresetStore::read("Sparkles", file));
    CHECK(std::strcmp(file.text, kLook) == 0);
    const char* changed = R"({"$slot":1,"Effects":{"enabled":false}})";
    const auto updated = mm::PresetStore::put("Sparkles", changed, mm::PresetStore::Mode::Match, created.etag);
    REQUIRE(updated);
    CHECK(mm::PresetStore::put("Sparkles", kLook, mm::PresetStore::Mode::Match, created.etag).status == 412);
    CHECK(mm::PresetStore::erase("Sparkles", created.etag).status == 412);
    REQUIRE(mm::PresetStore::erase("Sparkles", updated.etag));
    CHECK(mm::PresetStore::read("Sparkles", file).status == 404);
}

TEST_CASE("Native preset index snapshots retain all 64 names and share the complete index item wire format") {
    PresetDevice device;
    mm::PresetStore::IndexSnapshot snapshot{};
    REQUIRE(mm::PresetStore::snapshotIndex(snapshot));
    CHECK(snapshot.count == 0);
    REQUIRE(mm::platform::fsMkdir(mm::PresetStore::kDirectory));
    for (unsigned i = 0; i < mm::PresetStore::kMaxPresets; ++i) {
        char path[96];
        std::snprintf(path, sizeof(path), "%s/Look %02u.json", mm::PresetStore::kDirectory, i);
        REQUIRE(mm::platform::fsWriteAtomic(path, kLook, std::strlen(kLook)));
    }
    REQUIRE(mm::PresetStore::snapshotIndex(snapshot));
    REQUIRE(snapshot.count == 64);
    bool seen[64] = {};
    mm::JsonSink streamed;
    streamed.append("\"items\":[");
    for (unsigned i = 0; i < snapshot.count; ++i) {
        unsigned number = 64;
        REQUIRE(std::sscanf(snapshot.names[i], "Look %u", &number) == 1);
        REQUIRE(number < 64);
        CHECK_FALSE(seen[number]);
        seen[number] = true;
        if (i) streamed.append(",");
        REQUIRE(mm::PresetStore::writeIndexItem(snapshot.names[i], streamed));
    }
    streamed.append("],\"limit\":64");
    mm::JsonSink complete;
    REQUIRE(mm::PresetStore::writeIndex(complete));
    REQUIRE_FALSE(streamed.overflowed());
    CHECK(std::string(streamed.data(), streamed.size()) == std::string(complete.data(), complete.size()));
    for (bool present : seen) CHECK(present);

    const std::string extra = std::string(mm::PresetStore::kDirectory) + "/Overflow.json";
    REQUIRE(mm::platform::fsWriteAtomic(extra.c_str(), kLook, std::strlen(kLook)));
    CHECK(mm::PresetStore::snapshotIndex(snapshot).status == 507);
    CHECK(snapshot.count == 0); // overflow cannot expose a usable truncated membership list
}

TEST_CASE("Native preset index snapshot freezes membership but reads each current content tag separately") {
    PresetDevice device;
    const auto original = mm::PresetStore::put("Look", kLook, mm::PresetStore::Mode::Create);
    REQUIRE(original);
    mm::PresetStore::IndexSnapshot snapshot{};
    REQUIRE(mm::PresetStore::snapshotIndex(snapshot));
    REQUIRE(snapshot.count == 1);
    const char* changed = R"({"$slot":7,"Effects":{"enabled":false}})";
    const auto updated = mm::PresetStore::put("Look", changed, mm::PresetStore::Mode::Match, original.etag);
    REQUIRE(updated);
    REQUIRE(std::strcmp(updated.etag, original.etag) != 0);
    mm::JsonSink item;
    REQUIRE(mm::PresetStore::writeIndexItem(snapshot.names[0], item));
    mm::json::JsonDoc doc;
    REQUIRE(mm::json::parse(item.data(), doc));
    const auto* tag = mm::json::member(doc, doc.rootNode(), "etag");
    REQUIRE(tag != nullptr);
    REQUIRE(tag->type == mm::json::JsonType::String);
    CHECK(std::strcmp(tag->str, updated.etag) == 0);
    CHECK(std::string(item.data(), item.size()).find("\"slot\":7") != std::string::npos);
    REQUIRE(mm::PresetStore::erase("Look", updated.etag));
    mm::JsonSink unavailable;
    CHECK(mm::PresetStore::writeIndexItem(snapshot.names[0], unavailable).status == 404);
    CHECK(unavailable.size() == 0);
    CHECK(snapshot.count == 1);
}

TEST_CASE("Native preset index distinguishes invalid JSON from a file allocation failure before item output") {
    PresetDevice device;
    REQUIRE(mm::platform::fsMkdir(mm::PresetStore::kDirectory));
    constexpr char broken[] = "{\"Effects\":";
    REQUIRE(mm::platform::fsWriteAtomic("/.config/presets/Broken.json", broken, sizeof(broken) - 1));
    mm::JsonSink invalid;
    REQUIRE(mm::PresetStore::writeIndexItem("Broken", invalid));
    CHECK(std::string(invalid.data(), invalid.size()).find("\"kind\":\"invalid\"") != std::string::npos);
    mm::JsonSink refused;
    mm::platform::setTestAllocRefusedFrom(1);
    const auto result = mm::PresetStore::writeIndexItem("Broken", refused);
    mm::platform::setTestAllocRefusedFrom(0);
    CHECK(result.status == 507);
    CHECK(refused.size() == 0);
}

TEST_CASE("Native look policy ignores metadata and refuses pins layouts services and driver replacement") {
    PresetDevice device;
    for (const char* body : {
             R"({"Drivers":{"pin":12}})", R"({"Layouts":{}})", R"({"Services":{}})", R"({"System":{}})",
             R"({"$origin":{"scene":"safe"},"Drivers":{"$patch":"replace","palette":2}})",
             R"({"Effects":{"Output":{"type":"PresetStoreDrivers","pin":12}}})"}) {
        CHECK(mm::PresetStore::put("Unsafe", body, mm::PresetStore::Mode::Create).status == 403);
    }
    CHECK(mm::PresetStore::put("Palette", R"({"Drivers":{"palette":2}})", mm::PresetStore::Mode::Create));
    CHECK(mm::PresetStore::put("Duplicate", R"({"Effects":{},"Effects":{}})", mm::PresetStore::Mode::Create).status == 400);
    CHECK(mm::PresetStore::put("Metadata", R"({"$origin":{"scene":"x"}})", mm::PresetStore::Mode::Create).status == 400);
    for (const char* name : {"", "../outside", "with.dot", "a/b", "a\\b", "abcdefghijklmnopqrstuvwxyz012345"})
        CHECK(mm::PresetStore::put(name, kLook, mm::PresetStore::Mode::Create).status == 400);
    CHECK(device.drivers->pin == 35);
}

TEST_CASE("Native preset filesystem failures preserve the prior document and report storage failure") {
    PresetDevice device;
    const auto saved = mm::PresetStore::put("Keep", kLook, mm::PresetStore::Mode::Create);
    REQUIRE(saved);
    std::filesystem::create_directory(device.root + "/.config/presets/Keep.json.tmp");
    CHECK(mm::PresetStore::put("Keep", R"({"Effects":{}})", mm::PresetStore::Mode::Match, saved.etag).status == 507);
    mm::PresetStore::File file;
    REQUIRE(mm::PresetStore::read("Keep", file));
    CHECK(std::strcmp(file.text, kLook) == 0);
    CHECK(std::strcmp(file.etag, saved.etag) == 0);
}

TEST_CASE("Native preset read allocation failure leaves the saved file intact") {
    PresetDevice device;
    REQUIRE(mm::PresetStore::put("Keep", kLook, mm::PresetStore::Mode::Create));
    mm::PresetStore::File file;
    mm::platform::setTestAllocRefusedFrom(1);
    const auto refused = mm::PresetStore::read("Keep", file);
    mm::platform::setTestAllocRefusedFrom(0);
    CHECK(refused.status == 507);
    REQUIRE(mm::PresetStore::read("Keep", file));
    CHECK(std::strcmp(file.text, kLook) == 0);
}

TEST_CASE("Control surface saves renames and deletes use the same native files as HTTP") {
    PresetDevice device;
    REQUIRE(device.scheduler.setControl("Control", "source", R"({"value":"Effects"})") == mm::Scheduler::SetControlResult::Ok);
    REQUIRE(device.scheduler.setControl("Control", "name", R"({"value":"From surface"})") == mm::Scheduler::SetControlResult::Ok);
    REQUIRE(device.scheduler.setControl("Control", "save", R"({"value":1})") == mm::Scheduler::SetControlResult::Ok);
    mm::PresetStore::File file;
    REQUIRE(mm::PresetStore::read("From surface", file));
    CHECK(status(device.request("GET", "/api/presets/From%20surface"), 200));
    uint32_t row = 0;
    REQUIRE(device.control->listRowNamed("From surface", row));
    REQUIRE(device.control->setListRowField(row, "name", R"({"value":"Renamed"})"));
    CHECK(status(device.request("GET", "/api/presets/From%20surface"), 404));
    CHECK(status(device.request("GET", "/api/presets/Renamed"), 200));
    REQUIRE(device.control->listRowNamed("Renamed", row));
    REQUIRE(device.control->deleteListRow(row));
    CHECK(status(device.request("GET", "/api/presets/Renamed"), 404));
}

TEST_CASE("Preset HTTP exposes conditional CRUD and PUT never activates the stored animation") {
    PresetDevice device;
    CHECK(status(device.request("PUT", "/api/presets/Color%20Sparkles", kLook), 428));
    const auto made = device.request("PUT", "/api/presets/Color%20Sparkles", kLook, "If-None-Match: *\r\n");
    REQUIRE(status(made, 201));
    CHECK(made.find("\"applied\":false") != std::string::npos);
    CHECK(device.effect->speed == 7);
    CHECK(device.control->presetCount() == 1);
    const auto get = device.request("GET", "/api/presets/Color%20Sparkles");
    CHECK(get.find(kLook) != std::string::npos);
    mm::PresetStore::File file;
    REQUIRE(mm::PresetStore::read("Color Sparkles", file));
    CHECK(get.find(std::string("ETag: \"") + file.etag + "\"") != std::string::npos);
    const auto index = device.request("GET", "/api/presets");
    CHECK(index.find("\"originScene\":\"sparkles\"") != std::string::npos);
    CHECK(index.find("\"kind\":\"look\"") != std::string::npos);
    CHECK(status(device.request("POST", "/api/presets/Color%20Sparkles/apply", "{}", tagHeader(file.etag)), 200));
    CHECK(device.effect->speed == 61);
    CHECK(device.drivers->pin == 35);
    CHECK(std::strcmp(device.control->currentLook(), "Color Sparkles") == 0);
    const auto activeIndex = device.request("GET", "/api/presets");
    CHECK(activeIndex.find("\"active\":{\"effects\":\"Color Sparkles\"}") != std::string::npos);
    CHECK(status(device.request("PUT", "/api/presets/Color%20Sparkles?apply=if-active", kLook, tagHeader(file.etag)), 400));
    const auto updated = device.request("PUT", "/api/presets/Color%20Sparkles", R"({"Effects":{"Layer":{"Scene":{"speed":92}}}})", tagHeader(file.etag));
    REQUIRE(status(updated, 200));
    CHECK(device.effect->speed == 61);
    CHECK(status(device.request("DELETE", "/api/presets/Color%20Sparkles", {}, tagHeader(file.etag)), 412));
    REQUIRE(mm::PresetStore::read("Color Sparkles", file));
    CHECK(status(device.request("DELETE", "/api/presets/Color%20Sparkles"), 428));
    CHECK(status(device.request("DELETE", "/api/presets/Color%20Sparkles", {}, tagHeader(file.etag)), 200));
    CHECK(device.control->presetCount() == 0);
    CHECK(device.effect->speed == 61);
}

TEST_CASE("Preset HTTP requires explicit all scope for a hardware configuration and enforces strong tags") {
    PresetDevice device;
    constexpr const char* hardware = R"({"Drivers":{"pin":12}})";
    CHECK(status(device.request("PUT", "/api/presets/Hardware", hardware, "If-None-Match: *\r\n"), 403));
    REQUIRE(status(device.request("PUT", "/api/presets/Hardware?scope=all", hardware, "If-None-Match: *\r\n"), 201));
    mm::PresetStore::File file;
    REQUIRE(mm::PresetStore::read("Hardware", file));
    CHECK(status(device.request("POST", "/api/presets/Hardware/apply", "{}", tagHeader(file.etag)), 403));
    CHECK(device.drivers->pin == 35);
    CHECK(status(device.request("POST", "/api/presets/Hardware/apply", R"({"scope":"all"})", tagHeader(file.etag)), 200));
    CHECK(device.drivers->pin == 12);
    CHECK(status(device.request("DELETE", "/api/presets/Hardware", {}, "If-Match: *\r\n"), 400));
    CHECK(status(device.request("GET", "/api/presets/Hardware%00wrong"), 400));
    CHECK(status(device.request("GET", "/api/presets/Hardware%2Fwrong"), 400));
    CHECK(status(device.request("DELETE", "/api/presets/Hardware", {}, std::string("If-Match: W/\"") + file.etag + "\"\r\n"), 400));
}

TEST_CASE("Preset HTTP accepts complete documents larger than the short request buffer") {
    PresetDevice device;
    const std::string document = "{\"$note\":\"" + std::string(4000, 'x') + "\",\"Effects\":{}}";
    REQUIRE(status(device.request("PUT", "/api/presets/Large", document, "If-None-Match: *\r\n"), 201));
    CHECK(device.request("GET", "/api/presets/Large").find(document) != std::string::npos);
}

TEST_CASE("Preset output guard protects a foreign scene while keeping the edited file") {
    PresetDevice device;
    const auto created = mm::PresetStore::put("Sparkles", kLook, mm::PresetStore::Mode::Create);
    REQUIRE(created);
    mm::PresetStore::notifyChanged(device.scheduler, "Sparkles");
    const auto played = mm::PresetStore::apply(device.scheduler, "Sparkles", created.etag);
    REQUIRE(played);
    REQUIRE(std::strlen(played.outputEtag) == 64);
    CHECK(device.effect->speed == 61);
    const char* edit = R"({"Effects":{"Layer":{"Scene":{"speed":92}}}})";
    const auto updated = mm::PresetStore::put("Sparkles", edit, mm::PresetStore::Mode::Match, created.etag);
    REQUIRE(updated);
    device.effect->speed = 113;   // A write from another surface, independent of any remembered preset name.
    const auto stale = mm::PresetStore::apply(device.scheduler, "Sparkles", updated.etag, true, true, played.outputEtag);
    CHECK(stale.status == 412);
    CHECK(device.effect->speed == 113);
    mm::PresetStore::File file;
    REQUIRE(mm::PresetStore::read("Sparkles", file));
    CHECK(std::strcmp(file.text, edit) == 0);
    const auto explicitPlay = mm::PresetStore::apply(device.scheduler, "Sparkles", updated.etag);
    REQUIRE(explicitPlay);
    CHECK(device.effect->speed == 92);
    const auto same = mm::PresetStore::apply(device.scheduler, "Sparkles", updated.etag, true, true, explicitPlay.outputEtag);
    REQUIRE(same);
    CHECK(std::strcmp(same.outputEtag, explicitPlay.outputEtag) == 0);
}

TEST_CASE("Preset output tags exclude counters and driven values and include the actual palette") {
    PresetDevice device;
    char before[65], after[65];
    REQUIRE(mm::PresetStore::outputEtag(device.scheduler, before));
    device.effect->frames = 49;
    device.effect->phase = 128;
    REQUIRE(mm::PresetStore::outputEtag(device.scheduler, after));
    CHECK(std::strcmp(before, after) == 0);
    device.drivers->palette = 3;
    REQUIRE(mm::PresetStore::outputEtag(device.scheduler, after));
    CHECK(std::strcmp(before, after) != 0);
    std::memcpy(before, after, sizeof(before));
    std::snprintf(device.drivers->paletteScript, sizeof(device.drivers->paletteScript), "%s", "palette.cpp");
    REQUIRE(mm::PresetStore::outputEtag(device.scheduler, after));
    CHECK(std::strcmp(before, after) != 0);
}

TEST_CASE("Preset output hash allocation failure refuses guarded application") {
    PresetDevice device;
    const auto created = mm::PresetStore::put("Sparkles", kLook, mm::PresetStore::Mode::Create);
    REQUIRE(created);
    char output[65];
    REQUIRE(mm::PresetStore::outputEtag(device.scheduler, output));
    mm::platform::setTestAllocRefusedFrom(1);
    char refusedOutput[65] = {};
    const auto hash = mm::PresetStore::outputEtag(device.scheduler, refusedOutput);
    const auto applied = mm::PresetStore::apply(device.scheduler, "Sparkles", created.etag, true, true, output);
    mm::platform::setTestAllocRefusedFrom(0);
    CHECK(hash.status == 507);
    CHECK(applied.status == 507);
    CHECK(device.effect->speed == 7);
}

TEST_CASE("Preset HTTP output precondition fails before applying an old card") {
    PresetDevice device;
    const auto created = mm::PresetStore::put("Sparkles", kLook, mm::PresetStore::Mode::Create);
    REQUIRE(created);
    const auto played = device.request("POST", "/api/presets/Sparkles/apply", "{}", tagHeader(created.etag));
    REQUIRE(status(played, 200));
    CHECK(played.find("\"outputEtag\":") != std::string::npos);
    char output[65];
    REQUIRE(mm::PresetStore::outputEtag(device.scheduler, output));
    device.drivers->palette = 4;
    const std::string headers = tagHeader(created.etag) + "If-Output-Match: \"" + output + "\"\r\n";
    CHECK(status(device.request("POST", "/api/presets/Sparkles/apply", "{}", headers), 412));
    CHECK(device.drivers->palette == 4);
    mm::PresetStore::File file;
    REQUIRE(mm::PresetStore::read("Sparkles", file));
    CHECK(std::strcmp(file.etag, created.etag) == 0);
}

TEST_CASE("Preset template callbacks return documents without creating files or changing effects") {
    PresetDevice device;
    static constexpr mm::PresetTemplates templates{
        [](mm::JsonSink& sink) { sink.append(R"({"catalog":"test","items":[{"id":"sparkles","name":"Sparkles"}]})"); },
        [](mm::JsonSink& sink, mm::Scheduler&, const char* id) -> const char* {
            if (std::strcmp(id, "sparkles")) return "unknown template";
            sink.append(kLook); return nullptr;
        }};
    device.http.setPresetTemplates(&templates);
    CHECK(device.request("GET", "/api/preset-templates").find("\"catalog\":\"test\"") != std::string::npos);
    CHECK(device.request("GET", "/api/preset-templates/sparkles").find(kLook) != std::string::npos);
    CHECK(status(device.request("GET", "/api/preset-templates/missing"), 404));
    CHECK(device.effect->speed == 7);
    CHECK(device.control->presetCount() == 0);
}

TEST_CASE("Preset application remains successful when only its output guard allocation fails") {
    PresetDevice device;
    const auto saved = mm::PresetStore::put("Sparkles", kLook, mm::PresetStore::Mode::Create);
    REQUIRE(saved);
    device.effect->refuseAllocationsAfterPrepare = true;
    device.scheduler.requestPrepare(device.effects); // Inject refusal at the next actual scoped rebuild.
    const auto result = mm::PresetStore::apply(device.scheduler, "Sparkles", saved.etag);
    mm::platform::setTestAllocRefusedFrom(0);
    CHECK(result.status == 200);
    CHECK(result.applied);
    CHECK(result.state.ok);
    CHECK(result.outputEtag[0] == 0);
    CHECK(device.effect->speed == 61);
}

TEST_CASE("Preset engine errors retain their changes report and do not claim completed application") {
    PresetDevice device;
    const auto saved = mm::PresetStore::put("Broken", R"({"Effects":{"Layer":{"Scene":{"speed":92,"unknown":1}}}})", mm::PresetStore::Mode::Create);
    REQUIRE(saved);
    const auto result = mm::PresetStore::apply(device.scheduler, "Broken", saved.etag);
    CHECK(result.status == 400);
    CHECK_FALSE(result.applied);
    CHECK_FALSE(result.state.ok);
    CHECK(result.outputEtag[0] == 0);
    const auto reply = device.request("POST", "/api/presets/Broken/apply", "{}", tagHeader(saved.etag));
    CHECK(status(reply, 400));
    CHECK(reply.find("\"changes\":") != std::string::npos);
    CHECK(reply.find("\"applied\":true") == std::string::npos);
}

TEST_CASE("Preset HTTP reports a completed apply with a warning when its output token is unavailable") {
    PresetDevice device;
    const auto saved = mm::PresetStore::put("Sparkles", kLook, mm::PresetStore::Mode::Create);
    REQUIRE(saved);
    mm::PresetStore::notifyChanged(device.scheduler, "Sparkles");
    device.effect->refuseAllocationsAfterPrepare = true;
    device.scheduler.requestPrepare(device.effects); // Inject refusal at the next actual scoped rebuild.
    const auto reply = device.request("POST", "/api/presets/Sparkles/apply", "{}", tagHeader(saved.etag));
    mm::platform::setTestAllocRefusedFrom(0);
    REQUIRE(status(reply, 200));
    CHECK(reply.find("\"applied\":true") != std::string::npos);
    CHECK(reply.find("\"outputEtag\":null") != std::string::npos);
    CHECK(reply.find("\"warning\":\"output guard unavailable\"") != std::string::npos);
    CHECK(device.effect->speed == 61);
}

TEST_CASE("Preset mutation response allocation is reserved before create delete or apply") {
    PresetDevice device;
    const auto saved = mm::PresetStore::put("Keep", kLook, mm::PresetStore::Mode::Create);
    REQUIRE(saved);
    mm::platform::setTestAllocRefusedFrom(512);
    const auto create = device.request("PUT", "/api/presets/New", kLook, "If-None-Match: *\r\n");
    const auto remove = device.request("DELETE", "/api/presets/Keep", {}, tagHeader(saved.etag));
    const auto apply = device.request("POST", "/api/presets/Keep/apply", "{}", tagHeader(saved.etag));
    mm::platform::setTestAllocRefusedFrom(0);
    CHECK(status(create, 507));
    CHECK(status(remove, 507));
    CHECK(status(apply, 507));
    mm::PresetStore::File file;
    CHECK(mm::PresetStore::read("New", file).status == 404);
    REQUIRE(mm::PresetStore::read("Keep", file));
    CHECK(std::strcmp(file.etag, saved.etag) == 0);
    CHECK(device.effect->speed == 7);
}

TEST_CASE("System metadata identifies the product dashboard runtime for browser reloads") {
    PresetDevice device;
    const auto reply = device.request("GET", "/api/system");
    const size_t body = reply.find("\r\n\r\n");
    REQUIRE(body != std::string::npos);
    mm::json::JsonDoc doc;
    REQUIRE(mm::json::parse(reply.c_str() + body + 4, doc));
    const auto* ui = mm::json::member(doc, doc.rootNode(), "ui");
    REQUIRE(ui != nullptr);
    REQUIRE(ui->type == mm::json::JsonType::String);
    const bool product = mm::kProductFirmware;
    CHECK(std::strcmp(ui->str, product ? mm::ui::kProductRuntimeVersion : "") == 0);
}
