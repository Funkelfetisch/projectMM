/// @module BleControlService
#include "helio/BleProductProfiles.h"
#include "doctest.h"
#include "core/services/BleControlService.h"
#include "core/util/BleUpdateStream.h"
#include "helio/BleAdvertisementRegistry.h"
#include "helio/BlePixelMapPage.h"
#include "light/drivers/Drivers.h"
#include "light/drivers/FixtureProfilesModule.h"
#include "light/drivers/RmtLedDriver.h"
#include "light/layouts/Layouts.h"
#include "light/layouts/PixelMapLayout.h"
#include "core/services/BatteryService.h"
#include "core/system/ControlModule.h"
#include "core/util/ModuleFactory.h"
#include "core/util/BlePeerTable.h"
#include "core/util/fnv.h"
#include "core/util/sha256.h"
#include "platform/platform.h"
#include "core/util/build_info.h"
#include "module_types.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace {
constexpr const char* kBleTestEpoch = "00000000000000000000000000000001";
constexpr const char* kBleTestNonce = "00000000000000000000000000000011";
int scanSceneObservations = 0;
void observeScanScene(mm::BleAdvertisementSnapshot&, mm::Scheduler&) { ++scanSceneObservations; }

uint32_t replyNumber(const std::string& text, const char* key) {
    mm::json::JsonDoc doc;
    REQUIRE(mm::json::parse(text.c_str(), doc));
    const auto* node = mm::json::member(doc, doc.rootNode(), key);
    REQUIRE(node != nullptr);
    REQUIRE(node->type == mm::json::JsonType::Int);
    REQUIRE_FALSE(node->decimal);
    uint32_t value = 0;
    const auto parsed = std::from_chars(node->str, node->str + node->numLen, value);
    REQUIRE(parsed.ec == std::errc{});
    REQUIRE(parsed.ptr == node->str + node->numLen);
    return value;
}

std::string replyString(const std::string& text, const char* key) {
    mm::json::JsonDoc doc;
    REQUIRE(mm::json::parse(text.c_str(), doc));
    const auto* node = mm::json::member(doc, doc.rootNode(), key);
    REQUIRE(node != nullptr);
    REQUIRE(node->type == mm::json::JsonType::String);
    return node->str;
}

void checkEnvelope(const std::string& text, uint16_t id, uint32_t session, const char* operation) {
    CHECK(text.size() <= 512);
    CHECK(replyNumber(text, "v") == 1);
    CHECK(replyNumber(text, "id") == id);
    CHECK(replyNumber(text, "s") == session);
    CHECK(replyString(text, "e") == kBleTestEpoch);
    CHECK(replyString(text, "op") == operation);
    CHECK(replyNumber(text, "r") > 0);
    CHECK(text.find("\"now\":") != std::string::npos);
}

struct BleKnobs : mm::MoonModule {
    bool on = true;
    uint8_t brightness = 255;
    uint8_t palette = 0;
    bool rebuild = false;
    void defineControls() override {
        controls_.addControl("on", on);
        controls_.addControl("brightness", brightness, 0, 255);
        controls_.addControl("palette", palette, 0, 255);
    }
    bool affectsPrepare(const char*) const override { return rebuild; }
};
struct BleNetworkKnobs : mm::MoonModule {
    bool wifiEnabled = true;
    void defineControls() override { controls_.addControl("wifiEnabled", wifiEnabled); }
    bool affectsPrepare(const char*) const override { return false; }
};
struct BleTree {
    mm::Scheduler scheduler;
    BleKnobs* drivers = new BleKnobs();
    mm::BleControlService service;
    uint32_t session = 0;
    BleTree() {
        drivers->setName("Drivers");
        drivers->setTypeName("BleKnobs");
        drivers->defineControls();
        scheduler.addModule(drivers);
        service.setScheduler(&scheduler);
        service.setEpochForTest(kBleTestEpoch);
        hello(kBleTestNonce);
    }
    ~BleTree() { scheduler.release(); mm::platform::setTestBleLinkSecurity(mm::platform::BleLinkSecurity::Open); }
    std::string reply() const { return service.response(); }
    void hello(const char* nonce) {
        const std::string request = std::string("{\"$v\":1,\"$id\":1,\"$op\":\"hello\",\"$nonce\":\"") + nonce + "\"}";
        service.handle(request.c_str(), 100);
        REQUIRE(reply().find("\"ok\":true") != std::string::npos);
        session = replyNumber(reply(), "s");
        REQUIRE(session > 0);
    }
    std::string scoped(const char* request) const {
        mm::json::JsonDoc doc;
        if (!mm::json::parse(request, doc) || !doc.rootNode() || doc.rootNode()->type != mm::json::JsonType::Object) return request;
        std::string result = request;
        result.pop_back();
        result += ",\"$e\":\"" + std::string(kBleTestEpoch) + "\",\"$s\":" + std::to_string(session);
        const auto* op = mm::json::member(doc, doc.rootNode(), "$op");
        if (op && op->type == mm::json::JsonType::String && std::strcmp(op->str, "patch") == 0 &&
            !mm::json::member(doc, doc.rootNode(), "$live")) result += ",\"$live\":false";
        result += "}";
        return result;
    }
    void send(const char* request, uint32_t now) { service.handle(scoped(request).c_str(), now); }
    void finishPrepare(uint32_t now) { scheduler.preparePending(); service.applyDue(now); }
    void target(const char* operation, uint16_t id, uint32_t owner, uint16_t targetId, uint32_t now) {
        const std::string request = std::string("{\"$v\":1,\"$id\":") + std::to_string(id) +
            ",\"$op\":\"" + operation + "\",\"$target\":{\"s\":" + std::to_string(owner) +
            ",\"id\":" + std::to_string(targetId) + "}}";
        send(request.c_str(), now);
    }
};

std::string nestedReplyString(const std::string& text, const char* object, const char* key) {
    mm::json::JsonDoc doc;
    REQUIRE(mm::json::parse(text.c_str(), doc));
    const auto* parent = mm::json::member(doc, doc.rootNode(), object);
    REQUIRE(parent != nullptr);
    REQUIRE(parent->type == mm::json::JsonType::Object);
    const auto* node = mm::json::member(doc, parent, key);
    REQUIRE(node != nullptr);
    REQUIRE(node->type == mm::json::JsonType::String);
    return node->str;
}

struct BleEffectContainer : mm::MoonModule {
    const char* acceptsChildRoles() const override { return "generic,effect"; }
};

struct BleDeferredScript : mm::MoonModule {
    char source[16] = {};
    char compiled[16] = {};
    uint8_t late = 1;
    void defineControls() override {
        controls_.addText("source", source, sizeof(source));
        if (compiled[0]) controls_.addControl("late", late, 0, 100);
    }
    bool declaresControlsAtPrepare() const override { return true; }
    bool affectsPrepare(const char* key) const override { return std::strcmp(key, "source") == 0; }
    void prepare() override {
        if (std::strcmp(source, compiled) == 0) return;
        std::snprintf(compiled, sizeof(compiled), "%s", source);
        rebuildControls();
    }
};
}

TEST_CASE("BLE lighting patches use scheduler validation and read back actual driver state") {
    BleTree tree;
    tree.send(R"({"$v":1,"$id":1,"$op":"patch","Drivers":{"brightness":32,"on":false}})", 100);
    CHECK(tree.drivers->brightness == 32);
    CHECK_FALSE(tree.drivers->on);
    CHECK(tree.reply().find("\"ok\":true") != std::string::npos);
    CHECK(tree.reply().find("\"brightness\":32") != std::string::npos);
    CHECK(tree.reply().find("\"on\":false") != std::string::npos);
    tree.send(R"({"$v":1,"$id":2,"$op":"patch","Drivers":{"brightness":999}})", 100);
    CHECK(tree.drivers->brightness == 32);
    CHECK(tree.reply().find("\"ok\":false") != std::string::npos);
}

TEST_CASE("BLE battery reads on mains firmware return no invented pack") {
    BleTree tree;
    tree.send(R"({"$v":1,"$id":501,"$op":"battery"})", 1000);
    CHECK(tree.reply().find("\"ok\":true") != std::string::npos);
    CHECK(tree.reply().find("\"packs\":[]") != std::string::npos);
    CHECK(tree.reply().find("\"basChannels\":") == std::string::npos);
    tree.send(R"({"$v":1,"$id":502,"$op":"info"})", 1000);
    CHECK(tree.reply().find("\"basChannels\":0") != std::string::npos);
    CHECK(tree.reply().find("\"profileOp\":true") != std::string::npos);
    tree.send(R"({"$v":1,"$id":503,"$op":"get"})", 1000);
    CHECK(tree.reply().find("\"state\":") != std::string::npos);
}

TEST_CASE("BLE battery rejects multiple enabled services and recovers when corrected") {
    BleTree tree;
    auto* first = new mm::BatteryService();
    auto* second = new mm::BatteryService();
    first->setName("Battery1"); first->setTypeName("BatteryService");
    second->setName("Battery2"); second->setTypeName("BatteryService");
    auto* channel = new mm::BatteryChannel();
    channel->setTypeName("BatteryChannel"); channel->slot = 1;
    first->addChild(channel);
    tree.scheduler.addModule(first); tree.scheduler.addModule(second);
    bool ambiguous = false;
    CHECK(mm::BatteryService::discover(tree.scheduler, &ambiguous) == nullptr);
    CHECK(ambiguous);
    tree.send(R"({"$v":1,"$id":601,"$op":"battery"})", 1000);
    CHECK(tree.reply().find("\"error\":\"invalid-configuration\"") != std::string::npos);
    CHECK(tree.reply().find("\"packs\":[]") != std::string::npos);
    second->setEnabled(false);
    CHECK(mm::BatteryService::discover(tree.scheduler, &ambiguous) == first);
    CHECK_FALSE(ambiguous);
    tree.send(R"({"$v":1,"$id":602,"$op":"battery"})", 1000);
    CHECK(tree.reply().find("\"error\":") == std::string::npos);
    CHECK(tree.reply().find("\"id\":1") != std::string::npos);
}

TEST_CASE("BLE battery reads retain independent identities and fit the minimum-MTU long-read contract") {
    struct Clock { ~Clock() { mm::platform::setTestNowMs(0); } } clock;
    mm::platform::setTestNowMs(1000);
    BleTree tree;
    auto& battery = *new mm::BatteryService();
    battery.setName("Battery"); battery.setTypeName("BatteryService");
    tree.scheduler.addModule(&battery);
    auto& first = *new mm::BatteryChannel();
    auto& second = *new mm::BatteryChannel();
    battery.addChild(&first); battery.addChild(&second);
    first.setTypeName("BatteryChannel"); second.setTypeName("BatteryChannel");
    first.slot = 1; second.slot = 2;
    for (auto* channel : {&first, &second}) {
        channel->gainNumerator = 2; channel->gainDenominator = 1;
        channel->presence = 1; channel->emptyMv = 3000; channel->fullMv = 4200;
        channel->estimate = true; channel->staleSeconds = 1; channel->prepare();
    }
    first.acceptAdc(1800, 1000); second.acceptAdc(2000, 1000);
    CHECK((battery.batteryConfig() == mm::platform::BleBatteryConfig{2, {1, 2}}));
    tree.send(R"({"$v":1,"$id":503,"$op":"battery"})", 1000);
    CHECK(tree.reply().size() <= 512);
    CHECK(tree.reply().find("\"id\":1,\"sample\":") != std::string::npos);
    CHECK(tree.reply().find("\"id\":2,\"sample\":") != std::string::npos);
    CHECK(tree.reply().find("\"percent\":50") != std::string::npos);
    CHECK(tree.reply().find("\"percent\":83") != std::string::npos);
    const auto retained = tree.reply();
    mm::platform::setTestNowMs(2001); tree.service.tick1s();
    CHECK(tree.reply() == retained);
    tree.send(R"({"$v":1,"$id":504,"$op":"battery"})", 2001);
    CHECK(tree.reply().find("\"state\":\"stale\"") != std::string::npos);
    CHECK(tree.reply().find("\"percent\":null") != std::string::npos);
    CHECK(tree.reply().find("\"percent\":50") == std::string::npos);
    const auto snapshot = battery.snapshot(2001);
    CHECK(snapshot.slots[0].presenceKnown); CHECK(snapshot.slots[0].present);
    CHECK_FALSE(snapshot.slots[0].levelValid);
    // Only the second pack recovers: RPC2 and BAS must agree on its physical identity without making the first pack appear fresh or discharged.
    second.acceptAdc(1900, 2001);
    tree.send(R"({"$v":1,"$id":505,"$op":"battery"})", 2001);
    CHECK(tree.reply().find("\"id\":1,\"sample\":{\"presence\":\"present\",\"state\":\"stale\"") != std::string::npos);
    CHECK(tree.reply().find("\"id\":2,\"sample\":{\"presence\":\"present\",\"state\":\"fresh\"") != std::string::npos);
    CHECK(tree.reply().find("\"percent\":67") != std::string::npos);
    const auto recovered = battery.snapshot(2001);
    uint8_t level = 99;
    CHECK_FALSE(recovered.slots[0].readLevel(level)); CHECK(level == 99);
    REQUIRE(recovered.slots[1].readLevel(level)); CHECK(level == 67);
}

TEST_CASE("BLE battery reports invalid wiring instead of implying an absent battery") {
    BleTree tree;
    auto& battery = *new mm::BatteryService();
    battery.setName("Battery"); battery.setTypeName("BatteryService");
    tree.scheduler.addModule(&battery);
    auto& first = *new mm::BatteryChannel();
    auto& second = *new mm::BatteryChannel();
    first.setTypeName("BatteryChannel"); second.setTypeName("BatteryChannel");
    first.slot = 1; second.slot = 2;
    for (auto* channel : {&first, &second}) {
        channel->adcPin = 15; channel->gainNumerator = 2; channel->gainDenominator = 1;
    }
    battery.addChild(&first); battery.addChild(&second);
    tree.send(R"({"$v":1,"$id":1,"$op":"battery"})", 1000);
    CHECK(tree.reply().find("\"battery\":{\"sampling\":false,\"packs\":[],\"error\":\"invalid-configuration\"}") != std::string::npos);
    CHECK(tree.reply().size() <= 512);
}

TEST_CASE("BLE product identities still fit their fixed advertising contract after battery capabilities") {
    for (const char* firmware : {"helio1", "nebulite-s3", "nebulite-esp32"}) {
        BleTree tree; tree.service.setIdentity(firmware, "f5a244a9+"); tree.service.prepare();
        // Desktop has no radio: reaching this status proves the bounded identity was accepted before the stub.
        REQUIRE(tree.service.status() != nullptr);
        CHECK(std::string(tree.service.status()) == "BLE unavailable");
    }
}

TEST_CASE("BLE info exposes descriptive build metadata without changing identity") {
    BleTree tree;
    tree.service.setIdentity("helio1", "nogit", "6.1.0-dev", "Oct 29 2026 03:45:00");
    tree.send(R"({"$v":1,"$id":2,"$op":"info"})", 1000);
    CHECK(tree.reply().find("\"ok\":true") != std::string::npos);
    CHECK(replyString(tree.reply(), "firmwareVersion") == "6.1.0-dev");
    CHECK(replyString(tree.reply(), "builtAt") == "Oct 29 2026 03:45:00");
    CHECK(tree.reply().find("\"otaSlotBytes\"") == std::string::npos); // Desktop has no proven writable slot.
    CHECK(tree.reply().size() <= 512);
    mm::platform::setTestOtaNextAppCapacity(3145728);
    tree.send(R"({"$v":1,"$id":3,"$op":"info"})", 1001);
    CHECK(tree.reply().find("\"otaSlotBytes\":3145728") != std::string::npos);
    CHECK(tree.reply().size() <= 512);
    mm::platform::setTestOtaNextAppCapacity(0);
}

TEST_CASE("Product firmware registers the animations used by its catalogue") {
    if (!mm::kProductFirmware) return;
    mm::registerModuleTypes();
    auto* effect = mm::ModuleFactory::create("HelioColorTwinklesEffect");
    REQUIRE(effect != nullptr);
    delete effect;
}

TEST_CASE("BLE can disable and restore IP networking without admitting network settings") {
    BleTree tree;
    auto* network = new BleNetworkKnobs(); network->setName("Network"); network->defineControls();
    tree.scheduler.addModule(network);
    tree.send(R"({"$v":1,"$id":21,"$op":"patch","Network":{"wifiEnabled":false}})", 100);
    CHECK_FALSE(network->wifiEnabled);
    CHECK(tree.reply().find("\"wifiEnabled\":false") != std::string::npos);
    tree.send(R"({"$v":1,"$id":22,"$op":"patch","Network":{"wifiEnabled":true}})", 100);
    CHECK(network->wifiEnabled);
    for (const char* request : {
         R"({"$v":1,"$id":23,"$op":"patch","Network":{"wifiEnabled":false,"ssid":"changed"}})",
         R"({"$v":1,"$id":24,"$op":"patch","Network":{"wifiEnabled":"false"}})",
         R"({"$v":1,"$id":25,"$op":"patch","Network":{"wifiEnabled":false,"wifiEnabled":true}})",
         R"({"$v":1,"$id":26,"$op":"patch","Network":null})"}) {
        tree.send(request, 100);
        CHECK(tree.reply().find("\"error\":\"scope\"") != std::string::npos);
        CHECK(network->wifiEnabled);
    }
}

TEST_CASE("BLE telemetry remains valid JSON without a lighting tree") {
    mm::BleControlService service;
    service.handle(R"({"$v":1,"$id":1,"$op":"get"})", 100);
    mm::json::JsonDoc result;
    CHECK(mm::json::parse(service.response(), result));
}

TEST_CASE("BLE retry is idempotent and reusing a write id for a different document fails") {
    BleTree tree;
    const char* request = R"({"$v":1,"$id":1,"$op":"patch","Drivers":{"brightness":32}})";
    tree.send(request, 100);
    const auto retained = tree.reply();
    tree.drivers->brightness = 64;
    tree.send(request, 100);
    CHECK(tree.drivers->brightness == 64);
    CHECK(tree.reply() == retained);
    tree.send(R"({"$v":1,"$id":1,"$op":"patch","Drivers":{"brightness":99}})", 100);
    CHECK(tree.drivers->brightness == 64);
    CHECK(tree.reply().find("id-reused") != std::string::npos);
    tree.send(request, 100);
    CHECK(tree.reply() == retained);
    tree.send(R"({"$v":1,"$id":2,"$op":"get"})", 100);
    tree.send(request, 100);
    CHECK(tree.reply().find("id-expired") != std::string::npos);
    CHECK(tree.drivers->brightness == 64);
}

TEST_CASE("BLE malformed metadata and non-lighting roots are rejected before a lighting mutation") {
    BleTree tree;
    for (const char* request : {"{", R"({"$v":2,"$id":1,"$op":"patch"})",
         R"({"$v":1,"$id":1.1,"$op":"get"})", R"({"$v":1,"$id":1,"$op":"patch","Drivers":{"brightness":32},"Network":{}})",
         R"({"$v":1,"$id":2,"$op":"patch","Services":null})"}) {
        tree.send(request, 100);
        CHECK(tree.reply().find("\"ok\":false") != std::string::npos);
        CHECK(tree.drivers->brightness == 255);
    }
}

TEST_CASE("BLE scheduled groups require membership, bounded time and one pending write") {
    BleTree tree;
    tree.send(R"({"$v":1,"$id":1,"$op":"group","$group":"party"})", 100);
    CHECK(std::string(tree.service.group) == "party");
    tree.send(R"({"$v":1,"$id":2,"$op":"patch","$group":"elsewhere","Drivers":{"on":false}})", 100);
    CHECK(tree.drivers->on);
    CHECK(tree.reply().find("group-mismatch") != std::string::npos);
    tree.send(R"({"$v":1,"$id":3,"$op":"patch","$group":"party","$at":70000,"Drivers":{"on":false}})", 100);
    CHECK(tree.reply().find("time-window") != std::string::npos);
    tree.send(R"({"$v":1,"$id":4,"$op":"patch","$group":"party","$at":1000,"Drivers":{"on":false}})", 100);
    CHECK(tree.service.scheduled()); CHECK(tree.drivers->on);
    tree.send(R"({"$v":1,"$id":5,"$op":"patch","Drivers":{"brightness":32}})", 100);
    CHECK(tree.reply().find("busy") != std::string::npos);
    const std::string cancel = std::string(R"({"$v":1,"$id":6,"$op":"cancel","$target":{"s":)") + std::to_string(tree.session) + R"(,"id":4}})";
    tree.send(cancel.c_str(), 100);
    CHECK_FALSE(tree.service.scheduled()); CHECK(tree.drivers->on);
}

TEST_CASE("BLE queued patches execute at the deadline across uptime wrap and only once") {
    BleTree tree;
    tree.send(R"({"$v":1,"$id":10,"$op":"patch","$at":16,"Drivers":{"on":false}})", 0xfffffff0u);
    REQUIRE(tree.service.scheduled());
    tree.service.applyDue(15);
    CHECK(tree.drivers->on);
    tree.service.applyDue(16);
    CHECK_FALSE(tree.drivers->on);
    CHECK_FALSE(tree.service.scheduled());
    CHECK(tree.reply().find("\"queued\":false") != std::string::npos);
    tree.drivers->on = true;
    tree.service.applyDue(17);
    CHECK(tree.drivers->on);
}

TEST_CASE("BLE group members execute a common deadline in their own clock domains") {
    BleTree first;
    BleTree second;
    first.send(R"({"$v":1,"$id":1,"$op":"group","$group":"party"})", 100);
    second.send(R"({"$v":1,"$id":1,"$op":"group","$group":"party"})", 9300);
    first.send(R"({"$v":1,"$id":2,"$op":"patch","$live":true,"$group":"party","$at":2000,"Drivers":{"on":false}})", 1200);
    second.send(R"({"$v":1,"$id":2,"$op":"patch","$live":true,"$group":"party","$at":11200,"Drivers":{"on":false}})", 10400);
    REQUIRE(first.service.scheduled()); REQUIRE(second.service.scheduled());
    first.service.applyDue(1999); second.service.applyDue(11199);
    CHECK(first.drivers->on); CHECK(second.drivers->on);
    first.service.applyDue(2000); second.service.applyDue(11200);
    CHECK_FALSE(first.drivers->on); CHECK_FALSE(second.drivers->on);
    first.target("status", 3, first.session, 2, 2001);
    second.target("status", 3, second.session, 2, 11201);
    CHECK(nestedReplyString(first.reply(), "target", "state") == "applied");
    CHECK(nestedReplyString(second.reply(), "target", "state") == "applied");
}

TEST_CASE("BLE group mismatch rejects one member without cancelling another member") {
    BleTree first;
    BleTree second;
    first.send(R"({"$v":1,"$id":1,"$op":"group","$group":"party"})", 100);
    second.send(R"({"$v":1,"$id":1,"$op":"group","$group":"other"})", 100);
    const char* request = R"({"$v":1,"$id":2,"$op":"patch","$live":true,"$group":"party","$at":1000,"Drivers":{"on":false}})";
    first.send(request, 100); second.send(request, 100);
    REQUIRE(first.service.scheduled()); CHECK_FALSE(second.service.scheduled());
    CHECK(second.reply().find("group-mismatch") != std::string::npos);
    first.service.applyDue(1000); second.service.applyDue(1000);
    CHECK_FALSE(first.drivers->on); CHECK(second.drivers->on);
    first.target("status", 3, first.session, 2, 1001);
    CHECK(nestedReplyString(first.reply(), "target", "state") == "applied");
}

TEST_CASE("BLE protocol errors remain readable after periodic state refresh") {
    BleTree tree;
    tree.send(R"({"$v":1,"$id":11,"$op":"patch","Network":{}})", 100);
    tree.service.tick1s();
    CHECK(tree.reply().find("\"id\":11") != std::string::npos);
    CHECK(tree.reply().find("\"ok\":false") != std::string::npos);
    CHECK(tree.reply().find("scope") != std::string::npos);
}

TEST_CASE("BLE reads actual RGBW effect values without collapsing white into RGB") {
    struct Rgbw : mm::MoonModule {
        uint8_t red = 10, green = 20, blue = 30, white = 40;
        mm::ModuleRole role() const MM_NONBLOCKING override { return mm::ModuleRole::Effect; }
        void defineControls() override {
            controls_.addControl("red", red); controls_.addControl("green", green);
            controls_.addControl("blue", blue); controls_.addControl("white", white);
        }
    };
    BleTree tree;
    auto* effects = new mm::MoonModule(); effects->setName("Effects");
    auto* effect = new Rgbw(); effect->setName("Glow"); effect->setTypeName("RgbwExample"); effect->defineControls();
    effects->addChild(effect); tree.scheduler.addModule(effects);
    tree.send(R"({"$v":1,"$id":12,"$op":"effect"})", 100);
    CHECK(tree.reply().find("\"type\":\"RgbwExample\"") != std::string::npos);
    CHECK(tree.reply().find("\"red\":10") != std::string::npos);
    CHECK(tree.reply().find("\"white\":40") != std::string::npos);
}

TEST_CASE("BLE can change existing ambient controls but cannot replace services or disable its own transport") {
    struct Ambient : mm::MoonModule {
        bool automatic = true;
        void defineControls() override { controls_.addControl("automatic", automatic); }
    };
    BleTree tree;
    auto* services = new mm::MoonModule(); services->setName("Services");
    auto* ambient = new Ambient(); ambient->setName("Ambient Light"); ambient->setTypeName("Bh1750Service"); ambient->defineControls();
    services->addChild(ambient); tree.scheduler.addModule(services);
    tree.send(R"({"$v":1,"$id":13,"$op":"patch","Services":{"Ambient Light":{"automatic":false}},"Drivers":{"brightness":96}})", 100);
    CHECK_FALSE(ambient->automatic);
    CHECK(tree.drivers->brightness == 96);
    tree.finishPrepare(100);
    for (const char* request : {
        R"({"$v":1,"$id":14,"$op":"patch","Services":{"Ambient Light":null}})",
        R"({"$v":1,"$id":15,"$op":"patch","Services":{"Ambient Light":{"type":"Other"}}})",
        R"({"$v":1,"$id":16,"$op":"patch","Services":{"BLE":{"enabled":false}}})"}) {
        tree.send(request, 100);
        CHECK(tree.reply().find("scope") != std::string::npos);
    }
}

TEST_CASE("BLE get names the ambient-light service and its automatic switch, so the app can toggle auto brightness") {
    struct Ambient : mm::MoonModule {
        bool automatic = true;
        void defineControls() override { controls_.addControl("automatic", automatic); }
    };
    BleTree tree;
    tree.send(R"({"$v":1,"$id":20,"$op":"get"})", 100);
    CHECK(tree.reply().find("\"ambient\":") == std::string::npos);

    auto* services = new mm::MoonModule(); services->setName("Services");
    auto* ambient = new Ambient(); ambient->setName("Ambient Light"); ambient->setTypeName("Bh1750Service"); ambient->defineControls();
    services->addChild(ambient); tree.scheduler.addModule(services);
    tree.send(R"({"$v":1,"$id":21,"$op":"get"})", 100);
    CHECK(tree.reply().find(R"("ambient":{"service":"Ambient Light","automatic":true})") != std::string::npos);

    tree.send(R"({"$v":1,"$id":22,"$op":"patch","Services":{"Ambient Light":{"automatic":false}}})", 100);
    tree.finishPrepare(100);
    tree.send(R"({"$v":1,"$id":23,"$op":"get"})", 100);
    CHECK(tree.reply().find(R"("ambient":{"service":"Ambient Light","automatic":false})") != std::string::npos);

    ambient->setEnabled(false);
    tree.send(R"({"$v":1,"$id":24,"$op":"get"})", 100);
    CHECK(tree.reply().find("\"ambient\":") == std::string::npos);
}

namespace {
const char* fakeProfileState(const char* model) {
    return model && std::strcmp(model, "Test Garment") == 0 ? R"({"Drivers":{"brightness":77}})" : nullptr;
}
void fakeProfileNames(mm::JsonSink& sink) { sink.append(R"(["Test Garment"])"); }
}  // namespace

TEST_CASE("BLE setup applies a named product profile, so a garment with Wi-Fi off is set up from the phone") {
    BleTree tree;
    tree.send(R"({"$v":1,"$id":30,"$op":"profiles"})", 100);
    CHECK(tree.reply().find("profiles-unavailable") != std::string::npos);

    tree.service.setProductProfiles(&fakeProfileState, &fakeProfileNames);
    tree.send(R"({"$v":1,"$id":31,"$op":"info"})", 100);
    CHECK(tree.reply().find(R"("setupOp":true)") != std::string::npos);
    tree.send(R"({"$v":1,"$id":32,"$op":"profiles"})", 100);
    CHECK(tree.reply().find(R"("models":["Test Garment"])") != std::string::npos);

    tree.send(R"({"$v":1,"$id":33,"$op":"setup","$model":"Other","$live":false})", 100);
    CHECK(replyString(tree.reply(), "error") == "model");
    tree.send(R"({"$v":1,"$id":34,"$op":"setup","$model":"Test Garment","$live":false,"Drivers":{"brightness":5}})", 100);
    CHECK(replyString(tree.reply(), "error") == "fields");
    CHECK(tree.drivers->brightness == 255);

    tree.send(R"({"$v":1,"$id":35,"$op":"setup","$model":"Test Garment","$live":false})", 100);
    tree.finishPrepare(100);
    CHECK(tree.drivers->brightness == 77);
}

TEST_CASE("BLE setup offers only the catalogue products that name this firmware and chip") {
    namespace profiles = mm::ble_product_profiles;
    CHECK(profiles::lists("nebulite-s3,nebulite-esp32", "nebulite-esp32"));
    CHECK_FALSE(profiles::lists("nebulite-s3", "nebulite"));
    CHECK(profiles::stateFor("NEBULITE Croptop ESP32", "nebulite-esp32", "ESP32") != nullptr);
    CHECK(profiles::stateFor("NEBULITE Croptop ESP32", "nebulite-s3", "ESP32-S3") == nullptr);
    CHECK(profiles::stateFor("NEBULITE Croptop ESP32", "nebulite-esp32", "ESP32-S3") == nullptr);
    char buffer[512];
    mm::JsonSink sink(buffer, sizeof(buffer));
    profiles::writeNames(sink, "nebulite-esp32", "ESP32");
    const std::string names(buffer, sink.size());
    for (const char* product : {"NEBULITE Croptop ESP32", "NEBULITE Fanny pack ESP32", "NEBULITE Backpack ESP32", "NEBULITE Jacket ESP32"})
        CHECK(names.find(product) != std::string::npos);
    CHECK(names.find("S3") == std::string::npos);
    CHECK(names.find("HELIO necklace") == std::string::npos);
}

namespace {
const char* s3ProfileState(const char* model) { return mm::ble_product_profiles::stateFor(model, "nebulite-s3", "ESP32-S3"); }
void s3ProfileNames(mm::JsonSink& sink) { mm::ble_product_profiles::writeNames(sink, "nebulite-s3", "ESP32-S3"); }
struct DeviceModel : mm::MoonModule {
    char deviceModel[32] = "";
    void defineControls() override { controls_.addText("deviceModel", deviceModel, sizeof(deviceModel)); }
};
}  // namespace

TEST_CASE("BLE setup makes an S3 board the HELIO necklace: one ring of 43 GRBW lights on GPIO47, its rail on GPIO17") {
    mm::ModuleFactory::registerType<mm::RmtLedDriver>("RmtLedDriver");
    mm::ModuleFactory::registerType<mm::PixelMapLayout>("PixelMapLayout");
    mm::platform::clearTestGpioLevel();
    mm::Scheduler scheduler;
    auto* system = new DeviceModel(); system->setName("System"); system->defineControls();
    auto* drivers = new mm::Drivers(); drivers->setName("Drivers"); drivers->setTypeName("Drivers");
    // The fixture library is wired into Drivers on every board, so the setup's replace keeps it.
    auto* library = new mm::FixtureProfilesModule(); library->setName("FixtureProfiles"); library->setTypeName("FixtureProfilesModule");
    library->markWiredByCode(); drivers->addChild(library);
    auto* layouts = new mm::Layouts(); layouts->setName("Layouts"); layouts->setTypeName("Layouts");
    scheduler.addModule(system); scheduler.addModule(layouts); scheduler.addModule(drivers);
    scheduler.setup();
    mm::BleControlService service;
    service.setScheduler(&scheduler);
    service.setEpochForTest(kBleTestEpoch);
    service.setProductProfiles(&s3ProfileState, &s3ProfileNames);
    service.setAdvertisingProductResolver(&mm::ble_advertisement::productCodeForModel);
    service.handle(R"({"$v":1,"$id":1,"$op":"hello","$nonce":"00000000000000000000000000000041"})", 100);
    const uint32_t session = replyNumber(service.response(), "s");
    const auto send = [&](const char* request) {
        std::string scoped = request;
        scoped.pop_back();
        scoped += ",\"$e\":\"" + std::string(kBleTestEpoch) + "\",\"$s\":" + std::to_string(session) + "}";
        service.handle(scoped.c_str(), 100);
    };

    send(R"({"$v":1,"$id":2,"$op":"profiles"})");
    CHECK(std::string(service.response()).find("\"HELIO necklace\"") != std::string::npos);
    send(R"({"$v":1,"$id":3,"$op":"setup","$model":"HELIO necklace","$live":false})");
    scheduler.preparePending();
    service.applyDue(100);
    INFO(std::string(service.response()));
    CHECK(std::string(service.response()).find("\"ok\":true") != std::string::npos);

    CHECK(std::string(system->deviceModel) == "HELIO necklace");
    // The scan names the necklace with its own code; like the bags it carries no garment or sphere descriptor.
    CHECK(mm::encodeBleAdvertisement(service.advertisementSnapshot())[2] == 3);
    send(R"({"$v":1,"$id":4,"$op":"profile"})");
    CHECK(std::string(service.response()).find("\"profile\":null") != std::string::npos);
    CHECK(std::string(drivers->relayPins) == "17");
    REQUIRE(drivers->childCount() >= 1);
    const mm::RmtLedDriver* ring = nullptr;
    for (uint8_t i = 0; i < drivers->childCount(); ++i)
        if (std::strcmp(drivers->child(i)->typeName(), "RmtLedDriver") == 0) { CHECK(ring == nullptr); ring = static_cast<const mm::RmtLedDriver*>(drivers->child(i)); }
    REQUIRE(ring != nullptr);
    CHECK(std::string(ring->pins) == "47");
    CHECK(std::string(ring->ledsPerPin) == "43");
    // The SK6805SIDE sends green, red, blue, white, and its datasheet timing (T0H 0.2-0.4 us, T1H 0.62-1.0 us) is the default preset's.
    CHECK(std::string(ring->fixtureName()) == "GRBW");
    CHECK(ring->timing == mm::RmtLedDriver::kTiming800k);
    REQUIRE(layouts->childCount() == 1);
    REQUIRE(std::strcmp(layouts->child(0)->typeName(), "PixelMapLayout") == 0);
    const auto* map = static_cast<const mm::PixelMapLayout*>(layouts->child(0));
    CHECK(map->lightCount() == 43);
    // LED1 sits at the USB edge just above center, and the chain runs counter-clockwise seen from the dome.
    CHECK(std::strncmp(map->points, "14,6;14,5;14,4;", 15) == 0);

    // The rail is closed while the ring is on and bright, open when it is off.
    drivers->on = true; drivers->brightness = 100; drivers->onControlChanged("on");
    CHECK(mm::platform::gpioRead(17));
    drivers->on = false; drivers->onControlChanged("on");
    CHECK_FALSE(mm::platform::gpioRead(17));
    scheduler.release();
    mm::platform::clearTestGpioLevel();
}

TEST_CASE("BLE RPC2 new central sessions distinguish same-ID reads from a retained old-central result") {
    BleTree tree;
    uint32_t challenge = 32;
    for (const char* operation : {"get", "clock", "battery"}) {
        char nonce[33];
        std::snprintf(nonce, sizeof(nonce), "%032x", static_cast<unsigned>(challenge++));
        tree.hello(nonce);
        const std::string request = std::string("{\"$v\":1,\"$id\":1,\"$op\":\"") + operation + "\"}";
        tree.send(request.c_str(), 100);
        const std::string oldCentral = tree.reply();
        const uint32_t oldSession = tree.session;
        checkEnvelope(oldCentral, 1, oldSession, operation);
        REQUIRE(oldCentral.find("\"ok\":true") != std::string::npos);
        std::snprintf(nonce, sizeof(nonce), "%032x", static_cast<unsigned>(challenge++));
        tree.hello(nonce);
        REQUIRE(tree.session != oldSession);
        CHECK(replyNumber(oldCentral, "s") != tree.session);
        CHECK(replyString(tree.reply(), "op") == "hello");
        CHECK(replyString(tree.reply(), "nonce") == nonce);
        tree.send(request.c_str(), 101);
        checkEnvelope(tree.reply(), 1, tree.session, operation);
        CHECK(tree.reply().find("\"ok\":true") != std::string::npos);
    }
}

TEST_CASE("BLE RPC2 hello retry and explicit resume preserve the namespace and cached request") {
    BleTree tree;
    tree.send(R"({"$v":1,"$id":7,"$op":"get"})", 100);
    const std::string retained = tree.reply();
    const uint32_t originalSession = tree.session;
    tree.hello(kBleTestNonce);
    CHECK(tree.session == originalSession);
    CHECK(replyNumber(tree.reply(), "hi") == 7);
    tree.send(R"({"$v":1,"$id":7,"$op":"get"})", 101);
    CHECK(tree.reply() == retained);
    const std::string resume = std::string(R"({"$v":1,"$id":1,"$op":"hello","$nonce":"00000000000000000000000000000022","$resume":{"e":")") +
        kBleTestEpoch + "\",\"s\":" + std::to_string(originalSession) + "}}";
    tree.service.handle(resume.c_str(), 102);
    CHECK(replyNumber(tree.reply(), "s") == originalSession);
    CHECK(replyNumber(tree.reply(), "hi") == 7);
    tree.send(R"({"$v":1,"$id":7,"$op":"get"})", 103);
    CHECK(tree.reply() == retained);
}

TEST_CASE("BLE RPC2 reconnect retains an explicit resume and identical hello retry") {
    BleTree tree;
    tree.service.setReplyGenerationForTest(10); tree.hello(kBleTestNonce);
    tree.send(R"({"$v":1,"$id":7,"$op":"get"})", 100);
    const auto retained = tree.reply(); const auto original = tree.session;
    tree.service.setReplyGenerationForTest(20); tree.hello(kBleTestNonce);
    CHECK(tree.session == original); CHECK(replyNumber(tree.reply(), "hi") == 7);
    tree.send(R"({"$v":1,"$id":7,"$op":"get"})", 101); CHECK(tree.reply() == retained);
    tree.service.setReplyGenerationForTest(30);
    const std::string resume = std::string(R"({"$v":1,"$id":1,"$op":"hello","$nonce":"00000000000000000000000000000022","$resume":{"e":")") +
        kBleTestEpoch + "\",\"s\":" + std::to_string(original) + "}}";
    tree.service.handle(resume.c_str(), 102);
    CHECK(replyNumber(tree.reply(), "s") == original); CHECK(replyNumber(tree.reply(), "hi") == 7);
}

TEST_CASE("BLE RPC2 concurrent owners retain independent request caches") {
    BleTree tree;
    tree.service.setReplyGenerationForTest(10); tree.hello(kBleTestNonce);
    const uint32_t first = tree.session;
    tree.send(R"({"$v":1,"$id":7,"$op":"get"})", 100);
    const auto saved = tree.reply();
    tree.service.setConcurrentGenerationForTest(20);
    tree.hello("00000000000000000000000000000022");
    const uint32_t second = tree.session; REQUIRE(first != second);
    tree.send(R"({"$v":1,"$id":7,"$op":"battery"})", 101);
    checkEnvelope(tree.reply(), 7, second, "battery");
    tree.service.setConcurrentGenerationForTest(10); tree.session = first;
    tree.send(R"({"$v":1,"$id":7,"$op":"get"})", 102);
    CHECK(tree.reply() == saved);
    tree.service.setConcurrentGenerationForTest(20); tree.session = second;
    tree.send(R"({"$v":1,"$id":8,"$op":"get"})", 103);
    checkEnvelope(tree.reply(), 8, second, "get");
}

TEST_CASE("BLE RPC2 malformed hello does not occupy a peer slot or hide nonce errors") {
    BleTree tree;
    tree.service.setReplyGenerationForTest(10); tree.hello(kBleTestNonce);
    const auto first = tree.session;
    tree.service.setConcurrentGenerationForTest(20);
    tree.service.handle(R"({"$v":1,"$id":1,"$op":"hello","$nonce":"invalid"})", 100);
    CHECK(replyString(tree.reply(), "error") == "nonce");
    tree.hello("00000000000000000000000000000022");
    CHECK(tree.session != first);
    tree.service.setConcurrentGenerationForTest(10); tree.session = first;
    tree.send(R"({"$v":1,"$id":2,"$op":"get"})", 101);
    checkEnvelope(tree.reply(), 2, first, "get");
}

TEST_CASE("BLE RPC2 malformed resume nonce does not consume the retained session") {
    BleTree tree;
    tree.service.setReplyGenerationForTest(10); tree.hello(kBleTestNonce);
    const uint32_t original = tree.session;
    tree.service.setReplyGenerationForTest(20);
    const auto prefix = std::string(R"({"$v":1,"$id":1,"$op":"hello","$nonce":)");
    const auto suffix = std::string(R"(,"$resume":{"e":")") + kBleTestEpoch + "\",\"s\":" + std::to_string(original) + "}}";
    tree.service.handle((prefix + "\"bad\"" + suffix).c_str(), 100);
    CHECK(replyString(tree.reply(), "error") == "nonce");
    tree.service.handle((prefix + "\"00000000000000000000000000000022\"" + suffix).c_str(), 101);
    CHECK(replyNumber(tree.reply(), "s") == original);
}

TEST_CASE("BLE RPC2 concurrent owners can inspect and cancel a precisely named group job") {
    BleTree tree;
    tree.service.setReplyGenerationForTest(10); tree.hello(kBleTestNonce);
    const uint32_t first = tree.session;
    tree.send(R"({"$v":1,"$id":1,"$op":"group","$group":"party"})", 99);
    tree.send(R"({"$v":1,"$id":2,"$op":"patch","$live":true,"$group":"party","$at":1000,"Drivers":{"brightness":9}})", 100);
    REQUIRE(tree.service.scheduled());
    tree.service.setConcurrentGenerationForTest(20);
    tree.hello("00000000000000000000000000000022");
    const uint32_t second = tree.session; REQUIRE(second != first);
    tree.target("status", 2, first, 2, 102);
    checkEnvelope(tree.reply(), 2, second, "status");
    CHECK(nestedReplyString(tree.reply(), "target", "state") == "queued");
    tree.target("cancel", 3, first, 999, 103);
    CHECK(nestedReplyString(tree.reply(), "target", "state") == "not-retained");
    CHECK(tree.service.scheduled());
    tree.target("cancel", 4, first, 2, 104);
    CHECK(nestedReplyString(tree.reply(), "target", "state") == "cancelled");
    CHECK_FALSE(tree.service.scheduled());
    tree.service.applyDue(1000); CHECK(tree.drivers->brightness == 255);
    tree.service.setConcurrentGenerationForTest(10); tree.session = first;
    tree.target("status", 3, first, 2, 1001);
    checkEnvelope(tree.reply(), 3, first, "status");
    CHECK(nestedReplyString(tree.reply(), "target", "state") == "cancelled");
}

TEST_CASE("BLE RPC2 eight peers preserve a scheduled job when its disconnected slot is reused") {
    if constexpr (mm::platform::kBleControlPeerCapacity < 8) return;
    BleTree tree; tree.service.setEpochForTest(kBleTestEpoch);
    uint32_t sessions[8]{}; uint64_t owners[7]{}; size_t retained = 0;
    for (unsigned i = 0; i < 8; ++i) {
        char nonce[33]; std::snprintf(nonce, sizeof(nonce), "%032x", i + 1);
        tree.service.setConcurrentGenerationForTest(100 + i); tree.hello(nonce);
        sessions[i] = tree.session;
        for (unsigned j = 0; j < i; ++j) CHECK(sessions[i] != sessions[j]);
        if (i != 2) owners[retained++] = 100 + i;
    }
    tree.service.setConcurrentGenerationForTest(102); tree.session = sessions[2];
    tree.send(R"({"$v":1,"$id":2,"$op":"patch","$live":true,"$at":1000,"Drivers":{"brightness":9}})", 100);
    REQUIRE(tree.service.scheduled());
    tree.service.reconcileOwnersForTest(owners, retained);
    tree.service.setConcurrentGenerationForTest(108); tree.hello("00000000000000000000000000000009");
    const uint32_t replacement = tree.session;
    CHECK(replacement != sessions[2]);
    tree.service.applyDue(1000); CHECK(tree.drivers->brightness == 9);
    tree.service.setConcurrentGenerationForTest(108); tree.session = replacement;
    tree.target("status", 2, sessions[2], 2, 1001);
    checkEnvelope(tree.reply(), 2, replacement, "status");
    CHECK(nestedReplyString(tree.reply(), "target", "state") == "applied");
    tree.service.setConcurrentGenerationForTest(100); tree.session = sessions[0];
    tree.send(R"({"$v":1,"$id":2,"$op":"get"})", 1002);
    checkEnvelope(tree.reply(), 2, sessions[0], "get");
}

TEST_CASE("BLE RPC2 peer transport keeps two clients correlated across disconnect") {
    BleTree tree;
    tree.service.setEpochForTest(kBleTestEpoch);
    mm::BlePeerTable<2> peers;
    using Result = mm::BleMessage::Result;
    using Publish = mm::BlePeerTable<2>::Publish;
    REQUIRE(peers.connect(10)); REQUIRE(peers.connect(20));
    auto send = [&](uint16_t handle, const std::string& request, uint32_t now) {
        const std::string line = request + "\n";
        const size_t split = line.size() / 2;
        CHECK(peers.feed(handle, reinterpret_cast<const uint8_t*>(line.data()), split) == Result::Partial);
        CHECK(peers.feed(handle, reinterpret_cast<const uint8_t*>(line.data() + split), line.size() - split) == Result::Ready);
        char bytes[513] = {}; uint64_t generation = 0;
        REQUIRE(peers.take(bytes, sizeof(bytes), generation) == request.size());
        tree.service.setConcurrentGenerationForTest(generation);
        tree.service.handle(bytes, now);
        REQUIRE(peers.publish(tree.reply().c_str(), generation) == Publish::Stored);
        return generation;
    };
    const std::string helloA = std::string(R"({"$v":1,"$id":1,"$op":"hello","$nonce":")") + kBleTestNonce + "\"}";
    const std::string helloB = R"({"$v":1,"$id":1,"$op":"hello","$nonce":"00000000000000000000000000000022"})";
    const auto genA = send(10, helloA, 100); const uint32_t first = replyNumber(tree.reply(), "s");
    send(20, helloB, 101); const uint32_t second = replyNumber(tree.reply(), "s"); REQUIRE(first != second);
    const auto get = [&](uint32_t session, uint32_t id) {
        return std::string("{\"$v\":1,\"$id\":") + std::to_string(id) +
            ",\"$op\":\"get\",\"$e\":\"" + kBleTestEpoch + "\",\"$s\":" + std::to_string(session) + "}";
    };
    send(10, get(first, 2), 102); const std::string aReply = peers.reply(10);
    send(20, get(second, 2), 103); const std::string bReply = peers.reply(20);
    checkEnvelope(aReply, 2, first, "get"); checkEnvelope(bReply, 2, second, "get");
    REQUIRE(peers.disconnect(10)); REQUIRE(peers.connect(10));
    CHECK(peers.publish("late reply", genA) == Publish::Stale);
    CHECK(std::string(peers.reply(20)) == bReply);
    // Mirror the service tick's active-owner reconciliation before the next request.
    uint64_t live[2] = {}; REQUIRE(peers.generations(live, 2) == 2);
    tree.service.reconcileOwnersForTest(live, 2);
    // The still-connected second client continues with its original namespace.
    send(20, get(second, 3), 104); checkEnvelope(tree.reply(), 3, second, "get");
    send(10, helloA, 105); CHECK(replyNumber(tree.reply(), "s") == first);
    send(10, get(first, 2), 106); CHECK(std::string(peers.reply(10)) == aReply);
}

TEST_CASE("BLE RPC2 publishes a due group result separately from another peer's reply") {
    BleTree tree; tree.service.setEpochForTest(kBleTestEpoch);
    mm::BlePeerTable<2> peers; REQUIRE(peers.connect(10)); REQUIRE(peers.connect(20));
    using Result = mm::BleMessage::Result; using Publish = mm::BlePeerTable<2>::Publish;
    auto send = [&](uint16_t handle, const std::string& request, uint32_t now, bool dueProcessed = false) {
        const std::string line = request + "\n";
        REQUIRE(peers.feed(handle, reinterpret_cast<const uint8_t*>(line.data()), line.size()) == Result::Ready);
        char bytes[513] = {}; uint64_t generation = 0;
        REQUIRE(peers.take(bytes, sizeof(bytes), generation) == request.size());
        tree.service.setConcurrentGenerationForTest(generation);
        tree.service.handle(bytes, now, dueProcessed);
        REQUIRE(peers.publish(tree.reply().c_str(), generation) == Publish::Stored);
        return generation;
    };
    const auto hello = [](const char* nonce) {
        return std::string(R"({"$v":1,"$id":1,"$op":"hello","$nonce":")") + nonce + "\"}";
    };
    const uint64_t firstGeneration = send(10, hello(kBleTestNonce), 100);
    const uint32_t firstSession = replyNumber(tree.reply(), "s");
    tree.session = firstSession;
    send(10, tree.scoped(R"({"$v":1,"$id":1,"$op":"group","$group":"party"})"), 101);
    send(10, tree.scoped(R"({"$v":1,"$id":2,"$op":"patch","$live":true,"$group":"party","$at":1000,"Drivers":{"brightness":9}})"), 102);
    REQUIRE(tree.service.scheduled());
    const uint64_t secondGeneration = send(20, hello("00000000000000000000000000000022"), 103);
    const uint32_t secondSession = replyNumber(tree.reply(), "s"); REQUIRE(firstSession != secondSession);
    tree.service.applyDue(1000);
    CHECK(tree.drivers->brightness == 9);
    CHECK(tree.service.replyGenerationForTest() == firstGeneration);
    const std::string terminal = tree.reply(); checkEnvelope(terminal, 2, firstSession, "patch");
    REQUIRE(peers.publish(terminal.c_str(), firstGeneration) == Publish::Stored);
    tree.session = secondSession;
    CHECK(send(20, tree.scoped(R"({"$v":1,"$id":2,"$op":"get"})"), 1000, true) == secondGeneration);
    checkEnvelope(tree.reply(), 2, secondSession, "get");
    CHECK(std::string(peers.reply(10)) == terminal);
    CHECK(std::string(peers.reply(20)) == tree.reply());
}

TEST_CASE("BLE RPC2 request ID exhaustion requires a fresh session rather than reusing numeric IDs") {
    BleTree tree;
    tree.send(R"({"$v":1,"$id":65535,"$op":"clock"})", 100);
    checkEnvelope(tree.reply(), 65535, tree.session, "clock");
    tree.send(R"({"$v":1,"$id":1,"$op":"get"})", 101);
    CHECK(tree.reply().find("id-expired") != std::string::npos);
    const uint32_t oldSession = tree.session;
    tree.hello("00000000000000000000000000000023");
    REQUIRE(tree.session != oldSession);
    tree.send(R"({"$v":1,"$id":1,"$op":"get"})", 102);
    checkEnvelope(tree.reply(), 1, tree.session, "get");
    CHECK(tree.reply().find("\"ok\":true") != std::string::npos);
}

TEST_CASE("BLE RPC2 rejects unscoped writes and does not treat false or missing correlation as a fallback") {
    BleTree tree;
    for (const char* request : {
         R"({"$v":1,"$id":1,"$op":"patch","Drivers":{"brightness":9}})",
         R"({"$v":1,"$id":2,"$op":"group","$group":"unscoped"})",
         R"({"$v":1,"$id":3,"$op":"cancel"})",
         R"({"$v":1,"$id":4,"$op":"patch","$e":false,"$s":1,"$live":true,"Drivers":{"brightness":9}})"}) {
        tree.service.handle(request, 100);
        CHECK(tree.reply().find("\"ok\":false") != std::string::npos);
        CHECK(tree.drivers->brightness == 255);
        CHECK(std::string(tree.service.group).empty());
        CHECK(replyString(tree.reply(), "e") == kBleTestEpoch);
        CHECK(replyNumber(tree.reply(), "r") > 0);
    }
}

TEST_CASE("BLE RPC2 error receipts identify the request and current epoch without consuming another session's ID") {
    BleTree tree;
    tree.service.handle(R"({"$v":1,"$id":42,"$op":"get","$e":"00000000000000000000000000000001","$s":99})", 100);
    checkEnvelope(tree.reply(), 42, 99, "get");
    CHECK(tree.reply().find("session-expired") != std::string::npos);
    tree.send(R"({"$v":1,"$id":42,"$op":"get"})", 101);
    CHECK(tree.reply().find("\"ok\":true") != std::string::npos);
    const std::string wrongEpoch = std::string(R"({"$v":1,"$id":43,"$op":"get","$e":"ffffffffffffffffffffffffffffffff","$s":)") +
        std::to_string(tree.session) + "}";
    tree.service.handle(wrongEpoch.c_str(), 102);
    checkEnvelope(tree.reply(), 43, tree.session, "get");
    CHECK(tree.reply().find("session-expired") != std::string::npos);
    tree.send(R"({"$v":1,"$id":43,"$op":"get"})", 103);
    CHECK(tree.reply().find("\"ok\":true") != std::string::npos);
}

TEST_CASE("BLE RPC2 patches declare transient or saved intent and do not infer it from a missing field") {
    BleTree tree;
    static int saves = 0;
    saves = 0;
    tree.scheduler.setNoteDirtyHook([] { ++saves; });
    tree.drivers->clearDirty();
    tree.send(R"({"$v":1,"$id":1,"$op":"patch","$live":true,"Drivers":{"brightness":64}})", 100);
    REQUIRE(tree.drivers->brightness == 64);
    CHECK_FALSE(tree.drivers->dirty());
    CHECK(saves == 0);
    tree.send(R"({"$v":1,"$id":2,"$op":"patch","$live":false,"Drivers":{"brightness":65}})", 101);
    REQUIRE(tree.drivers->brightness == 65);
    CHECK(tree.drivers->dirty());
    CHECK(saves > 0);
    const std::string missing = std::string(R"({"$v":1,"$id":3,"$op":"patch","$e":")") + kBleTestEpoch +
        "\",\"$s\":" + std::to_string(tree.session) + R"(,"Drivers":{"brightness":66}})";
    tree.service.handle(missing.c_str(), 102);
    CHECK(tree.reply().find("\"ok\":false") != std::string::npos);
    CHECK(tree.drivers->brightness == 65);
}

TEST_CASE("BLE RPC2 saves the final slider value even when it already matches the live value") {
    BleTree tree;
    static int saves = 0; saves = 0;
    tree.scheduler.setNoteDirtyHook([] { ++saves; }); tree.drivers->clearDirty();
    tree.send(R"({"$v":1,"$id":1,"$op":"patch","$live":true,"Drivers":{"brightness":64}})", 100);
    REQUIRE(tree.drivers->brightness == 64); CHECK_FALSE(tree.drivers->dirty()); CHECK(saves == 0);
    tree.send(R"({"$v":1,"$id":2,"$op":"patch","$live":false,"Drivers":{"brightness":64}})", 101);
    CHECK(tree.reply().find("\"ok\":true") != std::string::npos);
    CHECK(tree.drivers->brightness == 64); CHECK(tree.drivers->dirty()); CHECK(saves > 0);
}

TEST_CASE("BLE RPC2 a new central can inspect and cancel a named old-session pending operation") {
    BleTree tree;
    const uint32_t oldSession = tree.session;
    tree.send(R"({"$v":1,"$id":1,"$op":"patch","$live":true,"$at":1000,"Drivers":{"on":false}})", 100);
    REQUIRE(tree.service.scheduled());
    tree.hello("00000000000000000000000000000024");
    REQUIRE(tree.session != oldSession);
    REQUIRE(tree.service.scheduled());
    tree.target("status", 1, oldSession, 1, 101);
    checkEnvelope(tree.reply(), 1, tree.session, "status");
    CHECK(nestedReplyString(tree.reply(), "target", "state") == "queued");
    tree.target("cancel", 2, oldSession, 99, 102);
    CHECK(nestedReplyString(tree.reply(), "target", "state") == "not-retained");
    CHECK(tree.service.scheduled());
    tree.target("cancel", 3, oldSession, 1, 103);
    CHECK(nestedReplyString(tree.reply(), "target", "state") == "cancelled");
    CHECK_FALSE(tree.service.scheduled());
    CHECK(tree.drivers->on);
    tree.send(R"({"$v":1,"$id":4,"$op":"get"})", 104);
    CHECK(nestedReplyString(tree.reply(), "terminal", "state") == "cancelled");
}

TEST_CASE("BLE RPC2 an unidentified cancel cannot erase a pending operation") {
    BleTree tree;
    tree.send(R"({"$v":1,"$id":1,"$op":"patch","$live":true,"$at":1000,"Drivers":{"on":false}})", 100);
    REQUIRE(tree.service.scheduled());
    tree.send(R"({"$v":1,"$id":2,"$op":"cancel"})", 101);
    CHECK(tree.reply().find("\"ok\":false") != std::string::npos);
    CHECK(tree.service.scheduled());
    tree.service.applyDue(1000);
    CHECK_FALSE(tree.drivers->on);
    tree.target("cancel", 3, tree.session, 1, 1001);
    CHECK(nestedReplyString(tree.reply(), "target", "state") == "already-applied");
}

TEST_CASE("BLE RPC2 a scheduled write executes its original bytes after later read requests") {
    BleTree tree;
    tree.send(R"({"$v":1,"$id":1,"$op":"patch","$live":true,"$at":1000,"Drivers":{"brightness":9}})", 100);
    REQUIRE(tree.service.scheduled());
    tree.send(R"({"$v":1,"$id":2,"$op":"battery"})", 101);
    checkEnvelope(tree.reply(), 2, tree.session, "battery");
    tree.send(R"({"$v":1,"$id":3,"$op":"get"})", 102);
    tree.service.applyDue(1000);
    CHECK(tree.drivers->brightness == 9);
    tree.target("status", 4, tree.session, 1, 1001);
    CHECK(nestedReplyString(tree.reply(), "target", "state") == "applied");
}

TEST_CASE("BLE RPC2 a deferred result keeps its original connection generation") {
    BleTree tree;
    tree.service.setReplyGenerationForTest(11);
    tree.hello(kBleTestNonce);
    tree.send(R"({"$v":1,"$id":1,"$op":"patch","$live":true,"$at":1000,"Drivers":{"brightness":9}})", 100);
    REQUIRE(tree.service.scheduled());
    const uint32_t originalSession = tree.session;
    tree.service.setReplyGenerationForTest(22);
    tree.hello("00000000000000000000000000000022");
    tree.send(R"({"$v":1,"$id":2,"$op":"get"})", 101);
    CHECK(tree.service.replyGenerationForTest() == 22);
    tree.service.applyDue(1000);
    CHECK(tree.drivers->brightness == 9);
    CHECK(tree.service.replyGenerationForTest() == 11);
    tree.service.setReplyGenerationForTest(22);
    tree.target("status", 3, originalSession, 1, 1001);
    CHECK(nestedReplyString(tree.reply(), "target", "state") == "applied");
    CHECK(tree.service.replyGenerationForTest() == 22);
}

TEST_CASE("BLE RPC2 a request at the deadline retains its own connection generation") {
    BleTree tree;
    tree.service.setReplyGenerationForTest(11);
    tree.hello(kBleTestNonce);
    tree.send(R"({"$v":1,"$id":1,"$op":"patch","$live":true,"$at":1000,"Drivers":{"brightness":9}})", 100);
    REQUIRE(tree.service.scheduled());
    tree.service.setReplyGenerationForTest(22);
    const std::string request = std::string(R"({"$v":1,"$id":1,"$op":"hello","$nonce":"00000000000000000000000000000022"})");
    tree.service.handle(request.c_str(), 1000);
    tree.session = replyNumber(tree.reply(), "s");
    CHECK(tree.drivers->brightness == 9);
    CHECK(tree.service.replyGenerationForTest() == 22);
    checkEnvelope(tree.reply(), 1, tree.session, "hello");
}

TEST_CASE("BLE RPC2 a contended transport cannot overwrite an unpublished reply") {
    struct Clock { ~Clock() { mm::platform::setTestNowMs(0); } } clock;
    BleTree tree;
    tree.send(R"({"$v":1,"$id":1,"$op":"patch","$live":true,"$at":1000,"Drivers":{"brightness":9}})", 100);
    REQUIRE(tree.service.scheduled());
    const auto original = tree.reply();
    mm::platform::setTestNowMs(1000);
    // The desktop BLE transport is unavailable, so publishing fails as with a held transport lock.
    tree.service.tick();
    CHECK(tree.reply() == original);
    CHECK(tree.service.scheduled());
    CHECK(tree.drivers->brightness == 255);
}

TEST_CASE("BLE RPC2 structural completion waits for the real preparation boundary") {
    BleTree tree;
    tree.drivers->rebuild = true;
    tree.send(R"({"$v":1,"$id":1,"$op":"patch","$live":true,"Drivers":{"brightness":9}})", 100);
    REQUIRE(tree.scheduler.preparationPending());
    CHECK(tree.drivers->brightness == 9);
    tree.target("status", 2, tree.session, 1, 101);
    CHECK(nestedReplyString(tree.reply(), "target", "state") == "applying");
    tree.target("cancel", 3, tree.session, 1, 102);
    CHECK(nestedReplyString(tree.reply(), "target", "state") == "applying");
    tree.finishPrepare(103);
    tree.target("status", 4, tree.session, 1, 104);
    CHECK(nestedReplyString(tree.reply(), "target", "state") == "applied");
}

TEST_CASE("BLE RPC2 deferred control failures become failed terminal results after preparation") {
    mm::ModuleFactory::registerType<BleDeferredScript>("BleDeferredScript");
    BleTree tree;
    auto* effects = new BleEffectContainer(); effects->setName("Effects");
    tree.scheduler.addModule(effects);
    static int saves = 0;
    saves = 0;
    tree.scheduler.setNoteDirtyHook([] { ++saves; });
    tree.send(R"({"$v":1,"$id":1,"$op":"patch","$live":true,"Effects":{"Script":{"type":"BleDeferredScript","source":"a","late":200}}})", 100);
    REQUIRE(tree.scheduler.preparationPending());
    tree.target("status", 2, tree.session, 1, 101);
    CHECK(nestedReplyString(tree.reply(), "target", "state") == "applying");
    tree.scheduler.preparePending();
    REQUIRE_FALSE(tree.scheduler.preparationResult().ok);
    tree.scheduler.prepareTree();
    REQUIRE(tree.scheduler.preparationResult().ok);
    tree.service.applyDue(102);
    tree.target("status", 3, tree.session, 1, 103);
    CHECK(nestedReplyString(tree.reply(), "target", "state") == "failed");
    CHECK_FALSE(nestedReplyString(tree.reply(), "target", "error").empty());
    CHECK_FALSE(effects->dirty());
    CHECK(saves == 0);
}

TEST_CASE("BLE RPC2 rejects indirect self-disable through a real Control switch before changing either radio") {
    const auto exercise = [](bool throughBle) {
        mm::Scheduler scheduler;
        struct Release { mm::Scheduler& scheduler; ~Release() { scheduler.release(); } } release{scheduler};
        auto* services = new mm::MoonModule(); services->setName("Services");
        auto* ble = new mm::BleControlService(); ble->setName("BLE"); ble->setTypeName("BleControlService");
        ble->setScheduler(&scheduler); services->addChild(ble); scheduler.addModule(services);
        auto* control = new mm::ControlModule(); control->setName("Control"); control->setTypeName("ControlModule");
        scheduler.addModule(control);
        auto* network = new BleNetworkKnobs(); network->setName("Network"); scheduler.addModule(network);
        scheduler.setup();
        REQUIRE(ble->enabled()); REQUIRE(network->wifiEnabled);
        if (!throughBle) {
            const auto result = mm::applyStateDocument(scheduler,
                R"({"Network":{"wifiEnabled":false},"Control":{"switch1Target":"BLE.enabled","switch1":false}})",
                mm::StateSource::Live);
            REQUIRE(result.ok);
            CHECK_FALSE(ble->enabled());
            CHECK_FALSE(network->wifiEnabled);
            return;
        }
        ble->setEpochForTest(kBleTestEpoch);
        ble->handle(R"({"$v":1,"$id":1,"$op":"hello","$nonce":"00000000000000000000000000000011"})", 100);
        const uint32_t session = replyNumber(ble->response(), "s");
        const std::string request = std::string(R"({"$v":1,"$id":1,"$op":"patch","$live":true,"$e":")") +
            kBleTestEpoch + "\",\"$s\":" + std::to_string(session) +
            R"(,"Network":{"wifiEnabled":false},"Control":{"switch1Target":"BLE.enabled","switch1":false}})";
        ble->handle(request.c_str(), 101);
        const std::string response = ble->response();
        checkEnvelope(response, 1, session, "patch");
        CHECK(response.find("\"ok\":false") != std::string::npos);
        CHECK(response.find("scope") != std::string::npos);
        CHECK(ble->enabled());
        CHECK(network->wifiEnabled);
    };
    exercise(false);
    exercise(true);
}

TEST_CASE("BLE RPC2 two independent batteries keep full nullable telemetry inside the 512-byte result") {
    struct Clock { ~Clock() { mm::platform::setTestNowMs(0); } } clock;
    mm::platform::setTestNowMs(1000);
    BleTree tree;
    auto* battery = new mm::BatteryService(); battery->setName("Battery"); battery->setTypeName("BatteryService");
    tree.scheduler.addModule(battery);
    for (uint8_t slot : {1, 2}) {
        auto* channel = new mm::BatteryChannel(); channel->setTypeName("BatteryChannel"); channel->slot = slot;
        channel->gainNumerator = 2; channel->gainDenominator = 1; channel->presence = 1;
        channel->estimate = true; channel->emptyMv = 3000; channel->fullMv = 4200; channel->staleSeconds = 1;
        battery->addChild(channel); channel->prepare(); channel->acceptAdc(32767, 1000);
    }
    mm::platform::setTestNowMs(UINT32_MAX);
    tree.send(R"({"$v":1,"$id":65535,"$op":"battery"})", UINT32_MAX);
    checkEnvelope(tree.reply(), 65535, tree.session, "battery");
    CHECK(tree.reply().find("\"ok\":true") != std::string::npos);
    CHECK(tree.reply().find("\"id\":1,\"sample\":") != std::string::npos);
    CHECK(tree.reply().find("\"id\":2,\"sample\":") != std::string::npos);
    CHECK(tree.reply().find("\"voltageMv\":65534") != std::string::npos);
    CHECK(tree.reply().find("\"percent\":null") != std::string::npos);
    CHECK(tree.reply().find("\"ageMs\":4294966295") != std::string::npos);
    // Reserve the remaining decimal width of maximum session/revision values, not only this fixture's small counters.
    CHECK(tree.reply().size() + 18 <= 512);
}

TEST_CASE("BLE hello errors echo the fresh challenge instead of an old central's receipt") {
    BleTree tree;
    tree.service.handle(R"({"$v":1,"$id":1,"$op":"hello","$nonce":"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee","$resume":{"e":"00000000000000000000000000000000","s":123}})", 100);
    CHECK(tree.reply().find("\"ok\":false") != std::string::npos);
    CHECK(tree.reply().find("\"nonce\":\"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee\"") != std::string::npos);
    CHECK(tree.reply().find("session-expired") != std::string::npos);
}

TEST_CASE("BLE service scan hint follows the selected NEBULITE setup and forgets unknown models") {
    struct Model : mm::MoonModule {
        char name[32] = "NEBULITE Croptop S3";
        void defineControls() override { controls_.addText("deviceModel", name, sizeof(name)); }
    };
    mm::Scheduler scheduler;
    auto* model = new Model();
    model->setName("System");
    model->defineControls();
    scheduler.addModule(model);
    mm::BleControlService service;
    service.setScheduler(&scheduler);
    service.setAdvertisingProductResolver(&mm::ble_advertisement::productCodeForModel);
    CHECK(mm::encodeBleAdvertisement(service.advertisementSnapshot())[2] == 20);
    std::snprintf(model->name, sizeof(model->name), "%s", "NEBULITE Jacket S3");
    CHECK(mm::encodeBleAdvertisement(service.advertisementSnapshot())[2] == 22);
    std::snprintf(model->name, sizeof(model->name), "%s", "custom");
    CHECK(mm::encodeBleAdvertisement(service.advertisementSnapshot())[2] == 0);
    scheduler.release();
}

TEST_CASE("BLE product profile reports the configured source without claiming physical verification") {
    struct Model : mm::MoonModule {
        char name[32] = "NEBULITE Croptop S3";
        uint8_t diameter = 0;
        void defineControls() override {
            controls_.addText("deviceModel", name, sizeof(name));
            controls_.addControl("physicalDiameterCm", diameter);
        }
    };
    BleTree tree;
    auto* model = new Model();
    model->setName("System"); model->defineControls(); tree.scheduler.addModule(model);
    tree.service.setAdvertisingProductResolver(&mm::ble_advertisement::productCodeForModel);
    scanSceneObservations = 0;
    tree.service.setAdvertisingScene(&observeScanScene);
    tree.send(R"({"$v":1,"$id":65530,"$op":"profile"})", 1000);
    checkEnvelope(tree.reply(), 65530, tree.session, "profile");
    CHECK(tree.reply().find("\"code\":20,\"source\":\"selected-model\",\"form\":\"garment\"") != std::string::npos);
    CHECK(tree.reply().find("\"verified\":false,\"revision\":null,\"mapping\":null,\"diameterMm\":null") != std::string::npos);
    CHECK(tree.reply().size() + 18 <= 512);
    CHECK(scanSceneObservations == 0);
    std::snprintf(model->name, sizeof(model->name), "%s", "NEBULITE Jacket S3");
    tree.send(R"({"$v":1,"$id":65531,"$op":"profile"})", 1001);
    CHECK(tree.reply().find("\"code\":22") != std::string::npos);
    std::snprintf(model->name, sizeof(model->name), "%s", "custom");
    tree.send(R"({"$v":1,"$id":65532,"$op":"profile"})", 1002);
    CHECK(tree.reply().find("\"profile\":null") != std::string::npos);
    tree.send(R"({"$v":1,"$id":65533,"$op":"profile","$unexpected":1})", 1003);
    CHECK(tree.reply().find("\"error\":\"fields\"") != std::string::npos);
    tree.service.advertisementSnapshot();
    CHECK(scanSceneObservations == 1);
    tree.service.defineControls();
    tree.service.tick1s();
    const auto& controls = tree.service.controls();
    const char* httpProfile = nullptr;
    const mm::ControlDescriptor* profileControl = nullptr;
    for (uint8_t i = 0; i < controls.count(); ++i)
        if (std::strcmp(controls[i].name, "productProfile") == 0) {
            httpProfile = static_cast<const char*>(controls[i].ptr);
            profileControl = &controls[i];
        }
    REQUIRE(httpProfile != nullptr);
    REQUIRE(profileControl != nullptr);
    CHECK(std::string(httpProfile) == "null");
    std::snprintf(model->name, sizeof(model->name), "%s", "NEBULITE Croptop S3");
    tree.service.tick1s();
    CHECK(std::string(httpProfile).find("\"code\":20,\"source\":\"selected-model\"") != std::string::npos);
    char serialized[512]; mm::JsonSink wire(serialized, sizeof(serialized));
    mm::writeControlValue(wire, *profileControl);
    REQUIRE_FALSE(wire.overflowed());
    mm::json::JsonDoc controlValue;
    REQUIRE(mm::json::parse(serialized, controlValue));
    REQUIRE(controlValue.rootNode()->type == mm::json::JsonType::String);
    CHECK(std::string(controlValue.rootNode()->str) == std::string(httpProfile));
    tree.send(R"({"$v":1,"$id":65534,"$op":"profile"})", 1004);
    CHECK(tree.reply().find(std::string("\"profile\":") + httpProfile) != std::string::npos);
}

TEST_CASE("BLE sphere diameter is explicit physical metadata, never derived from LED count") {
    struct Model : mm::MoonModule {
        uint8_t diameter = 0;
        void defineControls() override { controls_.addControl("physicalDiameterCm", diameter); }
    };
    BleTree tree;
    auto* model = new Model(); model->setName("System"); model->defineControls(); tree.scheduler.addModule(model);
    tree.service.setAdvertisingProduct({1, 0});
    tree.send(R"({"$v":1,"$id":1,"$op":"profile"})", 100);
    CHECK(tree.reply().find("\"source\":\"firmware-target\",\"form\":\"sphere\"") != std::string::npos);
    CHECK(tree.reply().find("\"diameterMm\":null") != std::string::npos);
    model->diameter = 8;
    tree.send(R"({"$v":1,"$id":2,"$op":"profile"})", 101);
    CHECK(tree.reply().find("\"diameterMm\":80") != std::string::npos);
    // MINI80 is a distinct, still-unverified firmware target; an explicit diameter is metadata rather than proof of the physical board.
    tree.service.setAdvertisingProduct({2, 0});
    model->diameter = 0;
    tree.send(R"({"$v":1,"$id":3,"$op":"profile"})", 102);
    CHECK(tree.reply().find("\"code\":2,\"source\":\"firmware-target\",\"form\":\"sphere\"") != std::string::npos);
    CHECK(tree.reply().find("\"verified\":false,\"revision\":null,\"mapping\":null,\"diameterMm\":null") != std::string::npos);
    model->diameter = 8;
    tree.send(R"({"$v":1,"$id":4,"$op":"profile"})", 103);
    CHECK(tree.reply().find("\"diameterMm\":80") != std::string::npos);
}

TEST_CASE("BLE link diagnostics are read-only and unavailable without a radio") {
    mm::platform::BleControlHandle absent;
    mm::platform::BleLinkState status;
    CHECK_FALSE(mm::platform::bleControlLinkState(absent, status));
    CHECK(status.lastDisconnectReason == -1);
    CHECK(status.lastUpdateStatus == -1);
    CHECK(status.unsupportedAccess == 0);
    CHECK(status.activeConnections == 0);
    mm::BleControlService service;
    service.defineControls();
    service.tick1s();
    const mm::ControlDescriptor* diagnostics = nullptr;
    const auto& controls = service.controls();
    for (uint8_t i = 0; i < controls.count(); ++i)
        if (std::strcmp(controls[i].name, "link diagnostics") == 0) diagnostics = &controls[i];
    REQUIRE(diagnostics != nullptr);
    CHECK(diagnostics->type == mm::ControlType::ReadOnly);
    CHECK(std::string(static_cast<const char*>(diagnostics->ptr)) == "unavailable");
}

TEST_CASE("BLE pixel-map RPC reads bounded current coordinates and rejects stale pages") {
    BleTree tree;
    auto* layouts = new mm::Layouts(); layouts->setName("Layouts"); layouts->setTypeName("Layouts");
    auto* map = new mm::PixelMapLayout(); map->setName("ProductStrip"); map->setTypeName("PixelMapLayout");
    std::strcpy(map->points, "5,14;7,14;9,14;11,14;13,14;15,14;5,6;7,6;9,6;11,6;13,6;15,6;7,2;8,1;9,1;11,1;12,1;13,2;0,5;1,6;2,7;1,9;2,10;3,11;20,5;19,6;18,7;19,9;18,10;17,11;10,10");
    layouts->addChild(map); tree.scheduler.addModule(layouts);
    tree.service.setPixelMapReader(&mm::BlePixelMapPage::writeCurrent);
    tree.send(R"({"$v":1,"$id":2,"$op":"info"})",101);
    checkEnvelope(tree.reply(),2,tree.session,"info");
    CHECK(tree.reply().find("\"pixelMapOp\":true") != std::string::npos);
    tree.send(R"({"$v":1,"$id":3,"$op":"pixel-map","$offset":0})",102);
    checkEnvelope(tree.reply(),3,tree.session,"pixel-map");
    CHECK(tree.reply().find("\"total\":31") != std::string::npos);
    CHECK(tree.reply().find("\"next\":16") != std::string::npos);
    char rev[33]; mm::sha256Hex(map->points, std::strlen(map->points), rev);
    std::string request = std::string(R"({"$v":1,"$id":4,"$op":"pixel-map","$offset":16,"$rev":")") + rev + "\"}";
    tree.send(request.c_str(),103); checkEnvelope(tree.reply(),4,tree.session,"pixel-map");
    CHECK(tree.reply().find("[10,10]") != std::string::npos);
    CHECK(tree.reply().find("\"next\":null") != std::string::npos);
    map->points[0] = '6';
    request = std::string(R"({"$v":1,"$id":5,"$op":"pixel-map","$offset":16,"$rev":")") + rev + "\"}";
    tree.send(request.c_str(),104); checkEnvelope(tree.reply(),5,tree.session,"pixel-map");
    CHECK(tree.reply().find("\"error\":\"map-changed\"") != std::string::npos);
    tree.send(R"({"$v":1,"$id":6,"$op":"pixel-map","$offset":1,"$rev":4})",105);
    CHECK(tree.reply().find("\"error\":\"map-revision\"") != std::string::npos);
    map->setEnabled(false);
    tree.send(R"({"$v":1,"$id":7,"$op":"pixel-map"})",106);
    CHECK(tree.reply().find("\"map\":null") != std::string::npos);
}

TEST_CASE("BLE pixel-map RPC streams every page from one request, each continuation naming the first page's revision") {
    BleTree tree;
    auto* layouts = new mm::Layouts(); layouts->setName("Layouts"); layouts->setTypeName("Layouts");
    auto* map = new mm::PixelMapLayout(); map->setName("ProductStrip"); map->setTypeName("PixelMapLayout");
    std::strcpy(map->points, "5,14;7,14;9,14;11,14;13,14;15,14;5,6;7,6;9,6;11,6;13,6;15,6;7,2;8,1;9,1;11,1;12,1;13,2;0,5;1,6;2,7;1,9;2,10;3,11;20,5;19,6;18,7;19,9;18,10;17,11;10,10");
    layouts->addChild(map); tree.scheduler.addModule(layouts);
    tree.service.setPixelMapReader(&mm::BlePixelMapPage::writeCurrent);
    struct Recording { Recording() { mm::platform::setTestBlePublishRecorded(true); } ~Recording() { mm::platform::setTestBlePublishRecorded(false); } } recording;
    tree.send(R"({"$v":1,"$id":3,"$op":"pixel-map"})", 102);
    for (int tick = 0; tick < 4; ++tick) tree.service.tick();
    const auto replies = mm::platform::takeTestBlePublished();
    REQUIRE(replies.size() == 2);
    char rev[33]; mm::sha256Hex(map->points, std::strlen(map->points), rev);
    checkEnvelope(replies[0].reply, 3, tree.session, "pixel-map");
    CHECK(replies[0].reply.find("\"next\":16") != std::string::npos);
    checkEnvelope(replies[1].reply, 3, tree.session, "pixel-map");
    CHECK(replies[1].reply.find(std::string("\"revision\":\"") + rev + "\"") != std::string::npos);
    CHECK(replies[1].reply.find("[10,10]") != std::string::npos);
    CHECK(replies[1].reply.find("\"next\":null") != std::string::npos);
}

namespace {
// A BLE product with Network, its station and its access point wired as main wires them, on the desktop's radio seams, reached over a link the phone already encrypted.
struct BleWifiTree : BleTree {
    mm::NetworkModule* net = new mm::NetworkModule();
    mm::WiFiModule* wifi = new mm::WiFiModule();
    mm::AccessPointModule* ap = new mm::AccessPointModule();
    explicit BleWifiTree(bool apAvailable = false) {
        mm::platform::setTestNowMs(1000);
        mm::platform::setTestBleLinkSecurity(mm::platform::BleLinkSecurity::Encrypted);
        mm::platform::setTestWifiStaAvailable(true);
        mm::platform::setTestWifiApAvailable(apAvailable);
        net->setName("Network"); net->setTypeName("NetworkModule");
        for (mm::MoonModule* child : {static_cast<mm::MoonModule*>(wifi), static_cast<mm::MoonModule*>(ap)}) {
            child->markWiredByCode();
            net->addChild(child);
        }
        net->setWiFi(wifi); net->setAccessPoint(ap); net->allowWifiToggle(true);
        scheduler.addModule(net);
        net->rebuildControls(); wifi->rebuildControls();
        net->setup();
    }
    ~BleWifiTree() {
        mm::platform::setTestWifiScan(nullptr, 0);
        mm::platform::setTestWifiStaIPv4(nullptr);
        mm::platform::setTestWifiFailure(mm::platform::WifiFailure::None);
        mm::platform::setTestWifiStaAvailable(false);
        mm::platform::setTestWifiApAvailable(false);
        mm::platform::setTestNowMs(0);
    }
};

// The reply's wifi object, the reply itself held to the 512-byte long read.
struct WifiReply {
    mm::json::JsonDoc doc;
    const mm::json::JsonNode* wifi = nullptr;
    explicit WifiReply(const std::string& text) {
        CHECK(text.size() <= 512);
        REQUIRE(mm::json::parse(text.c_str(), doc));
        wifi = mm::json::member(doc, doc.rootNode(), "wifi");
        REQUIRE(wifi != nullptr);
    }
    const mm::json::JsonNode* field(const char* key) const {
        const auto* node = mm::json::member(doc, wifi, key);
        REQUIRE(node != nullptr);
        return node;
    }
    /// A string field's value, or "null".
    std::string text(const char* key) const {
        const auto* node = field(key);
        return node->type == mm::json::JsonType::String ? node->str : "null";
    }
};
}  // namespace

TEST_CASE("BLE WiFi setup lists the networks the device's own radio found, as many per reply as fit") {
    BleWifiTree tree;
    mm::platform::WifiNetwork found[16] = {};
    for (int i = 0; i < 16; ++i) {
        std::snprintf(found[i].ssid, sizeof(found[i].ssid), "venue-network-long-name-%02d", i);
        found[i].rssi = static_cast<int8_t>(-40 - i);
        found[i].secured = i % 2 == 0;
    }
    mm::platform::setTestWifiScan(found, 16);
    tree.wifi->remember("venue-network-long-name-03", "saved-pass");
    tree.send(R"({"$v":1,"$id":2,"$op":"wifi-scan"})", 1000);
    checkEnvelope(tree.reply(), 2, tree.session, "wifi-scan");
    CHECK(WifiReply(tree.reply()).text("scan") == "waiting");
    tree.net->tick1s();
    std::string names;
    uint32_t offset = 0, scanAt = 0;
    uint16_t id = 3;
    int pages = 0;
    for (bool more = true; more; ++pages) {
        const std::string request = R"({"$v":1,"$id":)" + std::to_string(id) + R"(,"$op":"wifi","$offset":)" + std::to_string(offset) + "}";
        tree.send(request.c_str(), 1000);
        checkEnvelope(tree.reply(), id++, tree.session, "wifi");
        WifiReply page(tree.reply());
        CHECK(page.text("scan") == "idle");
        if (!pages) scanAt = static_cast<uint32_t>(mm::json::readInt(page.field("scanAt"), 0));
        CHECK(static_cast<uint32_t>(mm::json::readInt(page.field("scanAt"), 0)) == scanAt);   // every page names the same scan
        const auto* networks = page.field("networks");
        REQUIRE(mm::json::arraySize(page.doc, networks) > 0);
        for (int k = 0; k < mm::json::arraySize(page.doc, networks); ++k) {
            const auto* row = mm::json::element(page.doc, networks, k);
            const auto* ssid = mm::json::element(page.doc, row, 0);
            REQUIRE(ssid != nullptr);
            const int index = static_cast<int>(offset) + k;
            CHECK(std::string(ssid->str) == found[index].ssid);
            CHECK(mm::json::readInt(mm::json::element(page.doc, row, 1), 0) == found[index].rssi);
            const int flags = (found[index].secured ? 1 : 0) | (index == 3 ? 2 : 0);   // secured, and known
            CHECK(mm::json::readInt(mm::json::element(page.doc, row, 2), -1) == flags);
            names += ssid->str;
        }
        const auto* next = page.field("next");
        more = next->type == mm::json::JsonType::Int;
        if (more) offset = static_cast<uint32_t>(mm::json::readInt(next, 0));
    }
    CHECK(scanAt > 0);
    CHECK(names.find("venue-network-long-name-15") != std::string::npos);
    CHECK(pages <= 3);   // sixteen long names in three round trips
}

TEST_CASE("BLE WiFi join is connect-first: the device joins at once and saves the network only once it joined") {
    BleWifiTree tree;
    tree.send(R"({"$v":1,"$id":2,"$op":"wifi-join","$ssid":"venue","$password":"secret-pass"})", 1000);
    checkEnvelope(tree.reply(), 2, tree.session, "wifi-join");
    CHECK(WifiReply(tree.reply()).text("join") == "joining");
    CHECK(tree.wifi->knownCount() == 0);
    tree.net->tick1s();
    CHECK(std::string(mm::platform::testLastStaSsid()) == "venue");
    tree.send(R"({"$v":1,"$id":3,"$op":"wifi"})", 1000);
    {
        WifiReply joining(tree.reply());
        CHECK(joining.text("ssid") == "venue");
        CHECK(joining.text("ip") == "null");
    }
    const uint8_t ip[4] = {192, 168, 4, 20};
    mm::platform::setTestWifiStaIPv4(ip);
    tree.net->tick1s();
    tree.send(R"({"$v":1,"$id":4,"$op":"wifi"})", 1000);
    WifiReply joined(tree.reply());
    CHECK(joined.text("join") == "joined");
    CHECK(joined.text("ssid") == "venue");
    CHECK(joined.text("ip") == "192.168.4.20");
    REQUIRE(tree.wifi->knownCount() == 1);
    CHECK(std::string(tree.wifi->passwordAt(0)) == "secret-pass");
}

TEST_CASE("BLE WiFi join with a wrong password says so and saves nothing") {
    BleWifiTree tree;
    tree.send(R"({"$v":1,"$id":2,"$op":"wifi-join","$ssid":"venue","$password":"wrong-pass"})", 1000);
    tree.net->tick1s();
    mm::platform::setTestWifiFailure(mm::platform::WifiFailure::WrongPassword);
    mm::platform::setTestNowMs(1000 + 11000);   // past the join window
    tree.net->tick1s();
    tree.send(R"({"$v":1,"$id":3,"$op":"wifi"})", 12000);
    WifiReply failed(tree.reply());
    CHECK(failed.text("join") == "wrong-password");
    CHECK(failed.text("ip") == "null");
    CHECK(tree.wifi->knownCount() == 0);
}

TEST_CASE("BLE WiFi join without a password uses the saved one, and a new password replaces it only once it joined") {
    BleWifiTree tree;
    tree.wifi->remember("home", "saved-pass");
    tree.send(R"({"$v":1,"$id":2,"$op":"wifi-join","$ssid":"home"})", 1000);
    REQUIRE(tree.wifi->joinRequest() != nullptr);
    CHECK(std::string(tree.wifi->joinRequest()->password) == "saved-pass");
    tree.send(R"({"$v":1,"$id":3,"$op":"wifi-join","$ssid":"home","$password":"typo-pass"})", 1000);
    tree.net->tick1s();
    mm::platform::setTestWifiFailure(mm::platform::WifiFailure::WrongPassword);
    mm::platform::setTestNowMs(1000 + 11000);
    tree.net->tick1s();
    CHECK(std::string(tree.wifi->passwordAt(0)) == "saved-pass");
}

TEST_CASE("BLE WiFi status shows the access point the device opened when no network joined") {
    BleWifiTree tree(true);
    tree.send(R"({"$v":1,"$id":2,"$op":"wifi"})", 1000);
    WifiReply status(tree.reply());
    CHECK(status.text("ap") == tree.ap->name());
    CHECK(status.text("ssid") == "null");
    CHECK(status.text("ip") == "null");
    CHECK(status.field("enabled")->type == mm::json::JsonType::Bool);
}

TEST_CASE("BLE WiFi setup is refused while the product's WiFi switch is off, which BLE turns back on") {
    BleWifiTree tree;
    tree.send(R"({"$v":1,"$id":2,"$op":"patch","Network":{"wifiEnabled":false}})", 1000);
    REQUIRE(tree.reply().find("\"ok\":true") != std::string::npos);
    tree.finishPrepare(1000);
    tree.send(R"({"$v":1,"$id":3,"$op":"wifi-scan"})", 1000);
    CHECK(replyString(tree.reply(), "error") == "wifi-disabled");
    tree.send(R"({"$v":1,"$id":4,"$op":"wifi-join","$ssid":"venue","$password":"secret-pass"})", 1000);
    CHECK(replyString(tree.reply(), "error") == "wifi-disabled");
    CHECK(tree.wifi->joinRequest() == nullptr);
    tree.send(R"({"$v":1,"$id":5,"$op":"wifi"})", 1000);
    CHECK(tree.reply().find("\"enabled\":false") != std::string::npos);
    tree.send(R"({"$v":1,"$id":6,"$op":"patch","Network":{"wifiEnabled":true}})", 1000);
    tree.finishPrepare(1000);
    tree.send(R"({"$v":1,"$id":7,"$op":"wifi-scan"})", 1000);
    CHECK(tree.reply().find("\"ok\":true") != std::string::npos);
}

TEST_CASE("BLE WiFi requests with a missing or oversized name, password or field are refused before any join") {
    BleWifiTree tree;
    uint16_t id = 2;
    for (const auto& [request, error] : std::initializer_list<std::pair<const char*, const char*>>{
             {R"("$op":"wifi-join","$password":"secret-pass")", "ssid"},
             {R"("$op":"wifi-join","$ssid":"")", "ssid"},
             {R"("$op":"wifi-join","$ssid":"123456789012345678901234567890123")", "ssid"},
             {R"("$op":"wifi-join","$ssid":"venue","$password":12345678)", "password"},
             {R"("$op":"wifi-join","$ssid":"venue","$password":"1234567890123456789012345678901234567890123456789012345678901234")", "password"},
             {R"("$op":"wifi-join","$ssid":"venue","$offset":0)", "fields"},
             {R"("$op":"wifi-scan","$ssid":"venue")", "fields"},
             {R"("$op":"wifi","$offset":256)", "offset"}}) {
        const std::string text = R"({"$v":1,"$id":)" + std::to_string(id) + "," + request + "}";
        tree.send(text.c_str(), 1000);
        CHECK(replyString(tree.reply(), "error") == error);
        ++id;
    }
    CHECK(tree.wifi->joinRequest() == nullptr);
}

TEST_CASE("BLE WiFi password travels only on an encrypted link, which the secure op asks the phone to pair") {
    BleWifiTree tree;
    mm::platform::setTestBleLinkSecurity(mm::platform::BleLinkSecurity::Open);
    tree.send(R"({"$v":1,"$id":2,"$op":"wifi-join","$ssid":"venue","$password":"secret-pass"})", 1000);
    CHECK(replyString(tree.reply(), "error") == "encryption-required");
    CHECK(tree.wifi->joinRequest() == nullptr);
    CHECK(mm::platform::testBleSecurityRequests() == 0);
    tree.send(R"({"$v":1,"$id":3,"$op":"secure"})", 1000);
    checkEnvelope(tree.reply(), 3, tree.session, "secure");
    CHECK(replyString(tree.reply(), "link") == "pairing");
    tree.send(R"({"$v":1,"$id":4,"$op":"secure"})", 1000);
    CHECK(replyString(tree.reply(), "link") == "pairing");
    CHECK(mm::platform::testBleSecurityRequests() == 1);
    mm::platform::setTestBleLinkSecurity(mm::platform::BleLinkSecurity::Encrypted);
    tree.send(R"({"$v":1,"$id":5,"$op":"secure"})", 1000);
    CHECK(replyString(tree.reply(), "link") == "encrypted");
    tree.send(R"({"$v":1,"$id":6,"$op":"wifi-join","$ssid":"venue","$password":"secret-pass"})", 1000);
    CHECK(WifiReply(tree.reply()).text("join") == "joining");
    CHECK(mm::platform::testBleSecurityRequests() == 0);
}

TEST_CASE("BLE WiFi join with the saved password needs no encryption, since no secret crosses the air") {
    BleWifiTree tree;
    mm::platform::setTestBleLinkSecurity(mm::platform::BleLinkSecurity::Open);
    tree.wifi->remember("home", "saved-pass");
    tree.send(R"({"$v":1,"$id":2,"$op":"wifi-join","$ssid":"home"})", 1000);
    REQUIRE(tree.wifi->joinRequest() != nullptr);
    CHECK(std::string(tree.wifi->joinRequest()->password) == "saved-pass");
}

TEST_CASE("BLE secure reports a failed pairing once, and the next request asks the phone again") {
    BleTree tree;
    mm::platform::setTestBleLinkSecurity(mm::platform::BleLinkSecurity::Failed);
    tree.send(R"({"$v":1,"$id":2,"$op":"secure"})", 1000);
    CHECK(replyString(tree.reply(), "link") == "failed");
    CHECK(mm::platform::testBleSecurityRequests() == 0);
    tree.send(R"({"$v":1,"$id":3,"$op":"secure"})", 1000);
    CHECK(replyString(tree.reply(), "link") == "pairing");
    CHECK(mm::platform::testBleSecurityRequests() == 1);
    tree.send(R"({"$v":1,"$id":4,"$op":"secure","$pair":true})", 1000);
    CHECK(replyString(tree.reply(), "error") == "fields");
}

TEST_CASE("BLE WiFi setup is offered only where a network module runs") {
    {
        BleTree tree;
        tree.send(R"({"$v":1,"$id":2,"$op":"info"})", 1000);
        CHECK(tree.reply().find("\"wifiOp\":false") != std::string::npos);
        tree.send(R"({"$v":1,"$id":3,"$op":"wifi"})", 1000);
        CHECK(replyString(tree.reply(), "error") == "wifi-unavailable");
    }
    BleWifiTree tree;
    tree.send(R"({"$v":1,"$id":2,"$op":"info"})", 1000);
    CHECK(tree.reply().find("\"wifiOp\":true") != std::string::npos);
    CHECK(tree.reply().size() <= 512);
}

// ---- Firmware update over BLE: a signed release, an ordered upload, and the connection that owns it.

namespace {
constexpr uint32_t kOtaSecurityVersion = 3;
constexpr uint32_t kOtaEsp32Target = 0x2aa0a5f1u;   // FNV-1a of "nebulite-esp32", the value firmware_signing.py computes for the same name

// A disposable public key: the host links no crypto library, so these tests pin what the service hands the primitive, and check_firmware_signature.py pins the P-256 arithmetic.
const std::array<uint8_t, 65> kOtaKey = [] {
    std::array<uint8_t, 65> key{};
    key[0] = 0x04;
    for (size_t i = 1; i < key.size(); ++i) key[i] = static_cast<uint8_t>(i * 7 + 1);
    return key;
}();

// The substitute primitive accepts exactly the signature a disposable scheme derives from the key and the signed digest.
std::array<uint8_t, 64> otaSignatureOf(const uint8_t* signedDigest) {
    std::array<uint8_t, 64> signature{};
    mm::Sha256 first, second;
    first.update(kOtaKey.data(), kOtaKey.size()); first.update(signedDigest, 32); first.digest(signature.data());
    second.update(signedDigest, 32); second.update(kOtaKey.data(), kOtaKey.size()); second.digest(signature.data() + 32);
    return signature;
}
bool otaSignatureCheck(const uint8_t* publicKey, size_t publicKeySize, const uint8_t* digest, size_t digestSize,
                       const uint8_t* signature, size_t signatureSize) {
    if (publicKeySize != kOtaKey.size() || std::memcmp(publicKey, kOtaKey.data(), kOtaKey.size()) != 0 ||
        digestSize != 32 || signatureSize != 64) return false;
    const auto expected = otaSignatureOf(digest);
    return std::memcmp(expected.data(), signature, 64) == 0;
}

std::string hexOf(const uint8_t* bytes, size_t count) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string text;
    for (size_t i = 0; i < count; ++i) { text.push_back(digits[bytes[i] >> 4]); text.push_back(digits[bytes[i] & 15]); }
    return text;
}
std::vector<uint8_t> otaImage(size_t size, uint8_t seed = 1) {
    std::vector<uint8_t> image(size);
    for (size_t i = 0; i < size; ++i) image[i] = static_cast<uint8_t>(i * 31 + seed);
    return image;
}

// What the publisher hands the phone: the 60 signed claims and the signature over their SHA-256, both as hex.
struct OtaRelease {
    std::vector<uint8_t> image;
    std::string claims, signature;
};
OtaRelease otaRelease(std::vector<uint8_t> image, uint32_t target = kOtaEsp32Target, uint32_t version = kOtaSecurityVersion) {
    std::array<uint8_t, 60> claims{};
    std::memcpy(claims.data(), "HELIO-FW-AUTH-V1", 16);
    const auto put = [&](size_t at, uint32_t value) { for (int i = 0; i < 4; ++i) claims[at + i] = static_cast<uint8_t>(value >> (24 - 8 * i)); };
    put(16, target); put(20, static_cast<uint32_t>(image.size())); put(24, version);
    mm::Sha256 imageHash; imageHash.update(image.data(), image.size()); imageHash.digest(claims.data() + 28);
    uint8_t signedDigest[32];
    mm::Sha256 claimsHash; claimsHash.update(claims.data(), claims.size()); claimsHash.digest(signedDigest);
    const auto signature = otaSignatureOf(signedDigest);
    return {std::move(image), hexOf(claims.data(), claims.size()), hexOf(signature.data(), signature.size())};
}

// The application slot the platform lacks on a desktop. It holds the update gate while open, as the platform's writer does.
struct FakeSlot : mm::FirmwareSlot {
    size_t room = 4096;
    bool gateHeld = false, failBegin = false, failWrite = false, failEnd = false;
    int failedAborts = 0;   // how many aborts report that cleanup is unconfirmed
    int begins = 0, commits = 0, aborts = 0;
    bool open = false;
    std::vector<uint8_t> image;
    size_t capacity() const override { return room; }
    bool busy() const override { return gateHeld || open; }
    bool begin(size_t) override {
        ++begins;
        if (failBegin || busy()) return false;
        open = true; image.clear();
        return true;
    }
    bool write(const uint8_t* data, size_t length) override {
        if (!open) return false;
        // A refused write frees the writer, as the platform does.
        if (failWrite) { open = false; return false; }
        image.insert(image.end(), data, data + length);
        return true;
    }
    bool end(bool commit) override {
        if (!open) return false;
        open = false;
        if (commit) ++commits;
        return !commit || !failEnd;
    }
    bool abort() override {
        ++aborts; open = false;
        return failedAborts > 0 ? (--failedAborts, false) : true;
    }
};

int otaRestarts = 0;
void countOtaRestart() { ++otaRestarts; }

// Two phones: [0] the default connection and [1] a second central, each with its own session and request ids.
struct OtaTree {
    FakeSlot slot;   // declared first so it outlives the service that aborts into it
    BleTree tree;
    struct Peer { uint64_t generation; uint32_t session; uint16_t id; };
    std::array<Peer, 2> peers{};
    size_t current = 0;
    uint32_t clock = 1000;   // a request without a time of its own happens at the last one's, since the idle clock must not run backwards
    uint32_t at(uint32_t now) { if (now) clock = now; return clock; }
    explicit OtaTree(bool available = true) {
        otaRestarts = 0;
        mm::platform::takeTestBleUpdateAcks();
        mm::platform::setTestFirmwareSignatureCheck(&otaSignatureCheck);
        tree.service.setIdentity("nebulite-esp32", "f5a244a9+", "7.0.0", "Nov  3 2026 10:00:00");
        if (available) { tree.service.setFirmwareRelease(kOtaKey, kOtaSecurityVersion); tree.service.setFirmwareSlotForTest(&slot); }
        tree.service.setRestartForTest(&countOtaRestart);
        peers[0] = {0, tree.session, 1};
        tree.service.setConcurrentGenerationForTest(20);
        tree.hello("00000000000000000000000000000022");
        peers[1] = {20, tree.session, 1};
        select(0);
    }
    ~OtaTree() { mm::platform::setTestFirmwareSignatureCheck(nullptr); }
    void select(size_t peer) {
        current = peer;
        tree.service.setConcurrentGenerationForTest(peers[peer].generation);
        tree.session = peers[peer].session;
    }
    std::string call(const char* op, const std::string& fields = "", uint32_t now = 0) {
        const std::string request = "{\"$v\":1,\"$id\":" + std::to_string(++peers[current].id) + ",\"$op\":\"" + op + "\"" + fields + "}";
        tree.send(request.c_str(), at(now));
        return tree.reply();
    }
    std::string begin(const OtaRelease& release, uint32_t now = 0) {
        return call("ota-begin", ",\"$claims\":\"" + release.claims + "\",\"$sig\":\"" + release.signature + "\"", now);
    }
    // One packet on the update data characteristic as the current phone writes it: the offset, bit 31 asking for an acknowledgement, then the bytes.
    bool packet(size_t offset, const uint8_t* bytes, size_t count, bool ask = false) {
        std::vector<uint8_t> frame(4 + count);
        const uint32_t word = static_cast<uint32_t>(offset) | (ask ? mm::BleUpdateStream::kAckRequest : 0u);
        for (size_t i = 0; i < 4; ++i) frame[i] = static_cast<uint8_t>(word >> (8 * i));
        std::copy(bytes, bytes + count, frame.begin() + 4);
        return tree.service.streamForTest(peers[current].generation, frame.data(), frame.size());
    }
    // One tick: the service stores what was streamed; the acknowledgements it sent.
    std::vector<mm::platform::BleUpdateAck> drain(uint32_t now = 0) { tree.service.applyDue(at(now)); return mm::platform::takeTestBleUpdateAcks(); }
    // A window as the phone streams it, in packets of up to 508 bytes whose last asks, then one tick; the acknowledgements.
    std::vector<mm::platform::BleUpdateAck> stream(size_t offset, const uint8_t* bytes, size_t count, uint32_t now = 0) {
        for (size_t done = 0; done < count; done += 508) {
            const size_t n = std::min<size_t>(508, count - done);
            packet(offset + done, bytes + done, n, done + n == count);
        }
        return drain(now);
    }
    std::vector<mm::platform::BleUpdateAck> stream(const OtaRelease& release, size_t offset, size_t count, uint32_t now = 0) {
        return stream(offset, release.image.data() + offset, count, now);
    }
    // What the one acknowledgement in `acks` says is stored; -1 when there is not exactly one, or it reports a failure.
    static long stored(const std::vector<mm::platform::BleUpdateAck>& acks) { return acks.size() == 1 && !acks[0].failed ? acks[0].stored : -1; }
    // Every window of the image in order; what the last acknowledgement says is stored.
    long streamAll(const OtaRelease& release, uint32_t now = 0) {
        long last = -1;
        for (size_t offset = 0; offset < release.image.size(); offset += 4096)
            last = stored(stream(release, offset, std::min<size_t>(4096, release.image.size() - offset), now));
        return last;
    }
    static bool ok(const std::string& reply) { return reply.find("\"ok\":true") != std::string::npos; }
    static std::string error(const std::string& reply) { return ok(reply) ? "" : replyString(reply, "error"); }
    std::string phase() { return replyString(call("ota-status"), "phase"); }
};
}  // namespace

TEST_CASE("BLE firmware update: the target is the variant name's FNV-1a, as the signer computes it") {
    CHECK(mm::fnv1a("nebulite-esp32", 14) == kOtaEsp32Target);
    CHECK(mm::fnv1a("nebulite-s3", 11) == 0x291b7dd2u);
    CHECK(mm::fnv1a("helio1", 6) == 0x1ab87b7du);
}

TEST_CASE("BLE firmware update is advertised only where this build can take one") {
    char text[mm::BleControlService::kIdentityCapacity];
    const mm::platform::BleBatteryConfig batteries{};
    {
        // No publisher key: the identity and info say nothing of updates and the operations do not exist.
        OtaTree off(false);
        mm::JsonSink identity(text, sizeof(text));
        off.tree.service.writeIdentity(identity, batteries);
        CHECK(std::string(text).find("\"bleOta\":false") != std::string::npos);
        const auto info = off.call("info");
        CHECK(info.find("otaTarget") == std::string::npos);
        CHECK(info.find("otaWindow") == std::string::npos);
        CHECK(OtaTree::error(off.call("ota-status")) == "operation");
    }
    {
        // A key alone is not an update path: the desktop has no application slot, so it stays false.
        BleTree desktop;
        desktop.service.setFirmwareRelease(kOtaKey, kOtaSecurityVersion);
        mm::JsonSink identity(text, sizeof(text));
        desktop.service.writeIdentity(identity, batteries);
        CHECK(std::string(text).find("\"bleOta\":false") != std::string::npos);
        desktop.send(R"({"$v":1,"$id":2,"$op":"ota-begin","$claims":"00","$sig":"00"})", 1000);
        CHECK(replyString(desktop.reply(), "error") == "operation");
    }
    {
        // A single-slot table (the classic USB layout beside MoonBase) has no inactive slot to write, so it offers no update.
        OtaTree single;
        single.slot.room = 0;
        mm::JsonSink identity(text, sizeof(text));
        single.tree.service.writeIdentity(identity, batteries);
        CHECK(std::string(text).find("\"bleOta\":false") != std::string::npos);
        CHECK(single.call("info").find("\"otaTarget\"") == std::string::npos);
    }
    OtaTree on;
    mm::JsonSink identity(text, sizeof(text));
    on.tree.service.writeIdentity(identity, batteries);
    CHECK_FALSE(identity.overflowed());
    CHECK(std::string(text).find("\"bleOta\":true") != std::string::npos);
    CHECK(std::strlen(text) < sizeof(text));
    const auto info = on.call("info");
    CHECK(replyNumber(info, "otaTarget") == kOtaEsp32Target);
    CHECK(replyNumber(info, "securityVersion") == kOtaSecurityVersion);
    CHECK(replyNumber(info, "otaWindow") == 4096);
    CHECK(replyNumber(info, "otaSlotBytes") == 4096);
}

namespace {
const char* fakePageReader(mm::JsonSink& sink, mm::Scheduler&, uint16_t, const char*) { sink.append("\"p\":0"); return nullptr; }
const char* fakeFrameReader(mm::JsonSink& sink, mm::Scheduler&, uint16_t, const char*, uint32_t) { sink.append("\"p\":0"); return nullptr; }
}  // namespace

TEST_CASE("BLE info with every capability and the update fields still fits the 512-byte reply") {
    struct Clock { ~Clock() { mm::platform::setTestNowMs(0); } } clock;
    struct Model : mm::MoonModule {
        char name[32] = "1234567890123456789012345678901";   // the longest model System accepts
        void defineControls() override { controls_.addText("deviceModel", name, sizeof(name)); }
    };
    OtaTree on;
    auto* model = new Model();
    model->setName("System"); model->defineControls(); on.tree.scheduler.addModule(model);
    on.tree.service.setIdentity("nebulite-esp32", "f5a244a9+", "7.0.0-rc.12+abcdef0", "Nov  3 2026 10:00:00");
    on.tree.service.setPixelMapReader(&fakePageReader);
    on.tree.service.setOutputReader(&fakePageReader);
    on.tree.service.setPreviewFrameReader(&fakeFrameReader);
    on.tree.service.setProductProfiles(&fakeProfileState, &fakeProfileNames);
    mm::platform::setTestNowMs(4294967295u);
    on.peers[0].id = 65534;
    const auto reply = on.call("info");
    CHECK(OtaTree::ok(reply));
    CHECK(reply.find("\"setupOp\":true") != std::string::npos);
    CHECK(reply.find("\"otaWindow\":4096") != std::string::npos);
    CHECK(reply.size() <= 512);
}

TEST_CASE("BLE firmware update: a signed release opens the slot, the streamed image commits it, and the restart is scheduled") {
    OtaTree t;
    t.slot.room = 12288;
    const auto release = otaRelease(otaImage(9000));
    CHECK(t.phase() == "idle");

    const auto begun = t.begin(release, 1000);
    REQUIRE(OtaTree::ok(begun));
    CHECK(replyNumber(begun, "total") == 9000);
    CHECK(replyNumber(begun, "window") == 4096);
    CHECK(t.slot.begins == 1);
    CHECK(t.phase() == "uploading");

    // Each window is answered once everything up to its last packet is stored.
    CHECK(OtaTree::stored(t.stream(release, 0, 4096, 1100)) == 4096);
    CHECK(OtaTree::stored(t.stream(release, 4096, 4096, 1200)) == 8192);
    const auto status = t.call("ota-status", "", 1250);
    CHECK(replyString(status, "phase") == "uploading");
    CHECK(replyNumber(status, "received") == 8192);
    CHECK(replyNumber(status, "total") == 9000);
    CHECK(OtaTree::stored(t.stream(release, 8192, 808, 1300)) == 9000);
    CHECK(t.slot.commits == 0);

    const auto ended = t.call("ota-end", "", 3000);
    REQUIRE(OtaTree::ok(ended));
    CHECK(ended.find("\"rebooting\":true") != std::string::npos);
    CHECK(t.slot.commits == 1);
    CHECK(t.slot.image == release.image);
    CHECK(t.phase() == "committed");

    // The restart waits 1.5 s after the reply and never delays the tick: it is scheduled, then fires when due.
    CHECK(t.tree.service.restartScheduledForTest());
    t.tree.service.applyDue(4499);
    CHECK(otaRestarts == 0);
    t.tree.service.applyDue(4500);
    CHECK(otaRestarts == 1);
    t.tree.service.applyDue(9000);
    CHECK(otaRestarts == 1);
}

TEST_CASE("BLE firmware update: a release that fails authorization never opens the slot") {
    OtaTree t;
    const auto image = otaImage(300);
    const auto good = otaRelease(image);

    const auto flipped = [](std::string text, size_t at) { text[at] = text[at] == 'a' ? 'b' : 'a'; return text; };
    auto tampered = good;
    tampered.signature = flipped(good.signature, 10);
    CHECK(OtaTree::error(t.begin(tampered)) == "ota-unauthorized");

    // A claim altered after signing: the image size (a nibble of its low byte, so it still fits the slot) and the image digest.
    auto alteredSize = good;
    alteredSize.claims = flipped(good.claims, 47);
    CHECK(OtaTree::error(t.begin(alteredSize)) == "ota-unauthorized");
    auto alteredDigest = good;
    alteredDigest.claims = flipped(good.claims, 60);
    CHECK(OtaTree::error(t.begin(alteredDigest)) == "ota-unauthorized");

    CHECK(OtaTree::error(t.begin(otaRelease(image, kOtaEsp32Target + 1))) == "ota-unauthorized");              // another variant's target
    CHECK(OtaTree::error(t.begin(otaRelease(image, kOtaEsp32Target, kOtaSecurityVersion - 1))) == "ota-unauthorized");   // older than the running release

    auto wrongKeySignature = good;
    wrongKeySignature.signature = std::string(128, 'a');
    CHECK(OtaTree::error(t.begin(wrongKeySignature)) == "ota-unauthorized");

    CHECK(t.slot.begins == 0);
    CHECK(t.phase() == "idle");
    // The same release, untouched, is accepted after all those refusals.
    CHECK(OtaTree::ok(t.begin(good)));
    CHECK(t.slot.begins == 1);
}

TEST_CASE("BLE firmware update: a release at the running security version or newer is accepted") {
    OtaTree t;
    CHECK(OtaTree::ok(t.begin(otaRelease(otaImage(64), kOtaEsp32Target, kOtaSecurityVersion))));
    CHECK(OtaTree::ok(t.call("ota-abort")));
    CHECK(OtaTree::ok(t.begin(otaRelease(otaImage(64), kOtaEsp32Target, kOtaSecurityVersion + 5))));
}

TEST_CASE("BLE firmware update: malformed claims and signatures are refused as unauthorized") {
    OtaTree t;
    const auto good = otaRelease(otaImage(64));
    const auto refused = [&](const std::string& fields) { return OtaTree::error(t.call("ota-begin", fields)) == "ota-unauthorized"; };
    CHECK(refused(""));
    CHECK(refused(",\"$claims\":\"" + good.claims + "\""));
    CHECK(refused(",\"$sig\":\"" + good.signature + "\""));
    CHECK(refused(",\"$claims\":\"" + good.claims.substr(2) + "\",\"$sig\":\"" + good.signature + "\""));
    CHECK(refused(",\"$claims\":\"" + good.claims + "00\",\"$sig\":\"" + good.signature + "\""));
    CHECK(refused(",\"$claims\":\"" + good.claims + "\",\"$sig\":\"" + good.signature.substr(2) + "\""));
    CHECK(refused(",\"$claims\":\"" + std::string(120, 'z') + "\",\"$sig\":\"" + good.signature + "\""));
    CHECK(refused(",\"$claims\":\"" + good.claims + "\",\"$sig\":\"" + std::string(128, 'g') + "\""));
    CHECK(refused(",\"$claims\":7,\"$sig\":\"" + good.signature + "\""));
    // The signed domain tag is part of the claims: "HELIO-FW-AUTH-V2" is not a release.
    auto otherDomain = good;
    otherDomain.claims[31] = '2';
    CHECK(refused(",\"$claims\":\"" + otherDomain.claims + "\",\"$sig\":\"" + good.signature + "\""));
    // A correctly signed release of zero bytes is no image.
    CHECK(OtaTree::error(t.begin(otaRelease({}))) == "ota-unauthorized");
    CHECK(t.slot.begins == 0);
}

TEST_CASE("BLE firmware update: an image the slot cannot hold is refused as a slot error, before anything opens") {
    OtaTree t;
    t.slot.room = 699;
    CHECK(OtaTree::error(t.begin(otaRelease(otaImage(700)))) == "ota-slot");
    t.slot.room = 0;   // no inactive slot at all: no update path, so the operation itself is unavailable
    CHECK(OtaTree::error(t.begin(otaRelease(otaImage(10)))) == "operation");
    CHECK(t.slot.begins == 0);
    t.slot.room = 700;
    CHECK(OtaTree::ok(t.begin(otaRelease(otaImage(700)))));
}

TEST_CASE("BLE firmware update: another update holding the gate keeps this one out, and a failed open is an I/O error") {
    OtaTree t;
    const auto release = otaRelease(otaImage(300));
    t.slot.gateHeld = true;   // an HTTP, URL or MQTT update is under way
    CHECK(OtaTree::error(t.begin(release)) == "ota-busy");
    CHECK(t.slot.begins == 0);
    t.slot.gateHeld = false;
    t.slot.failBegin = true;
    CHECK(OtaTree::error(t.begin(release)) == "ota-io");
    t.slot.failBegin = false;
    CHECK(OtaTree::ok(t.begin(release)));
    // The gate this transfer holds is the one that keeps the others out.
    CHECK(t.slot.busy());
}

TEST_CASE("BLE firmware update: a packet past what is held is dropped, and the answer says where to resume") {
    OtaTree t;
    const auto release = otaRelease(otaImage(3000));
    REQUIRE(OtaTree::ok(t.begin(release)));
    // The window's first packet is lost: the rest leave a gap, so the answer names what is stored.
    t.packet(508, release.image.data() + 508, 508);
    t.packet(1016, release.image.data() + 1016, 508, true);
    CHECK(OtaTree::stored(t.drain()) == 0);
    CHECK(t.slot.image.empty());
    // The phone resends from there, and the transfer completes.
    CHECK(OtaTree::stored(t.stream(release, 0, 1524)) == 1524);
    CHECK(OtaTree::stored(t.stream(release, 1524, 1476)) == 3000);
    CHECK(OtaTree::ok(t.call("ota-end")));
    CHECK(t.slot.image == release.image);
}

TEST_CASE("BLE firmware update: a resent packet overlapping what is held stores nothing twice") {
    OtaTree t;
    const auto release = otaRelease(otaImage(1200));
    REQUIRE(OtaTree::ok(t.begin(release)));
    CHECK(OtaTree::stored(t.stream(release, 0, 1000)) == 1000);
    // The answer was lost, so the phone resends the window's last packet and is answered again.
    CHECK(OtaTree::stored(t.stream(release, 492, 508)) == 1000);
    // The next window overlaps what arrived; only the new bytes are stored.
    CHECK(OtaTree::stored(t.stream(release, 800, 400)) == 1200);
    CHECK(t.slot.image == release.image);
}

TEST_CASE("BLE firmware update: a packet too short to carry an offset and a byte is ignored") {
    OtaTree t;
    const auto release = otaRelease(otaImage(700));
    REQUIRE(OtaTree::ok(t.begin(release)));
    const uint8_t header[4] = {0, 0, 0, 0x80};
    CHECK_FALSE(t.tree.service.streamForTest(0, header, sizeof(header)));
    CHECK_FALSE(t.tree.service.streamForTest(0, nullptr, 0));
    CHECK(t.drain().empty());
    CHECK(t.slot.image.empty());
    CHECK(OtaTree::stored(t.stream(release, 0, 1)) == 1);       // the smallest packet
    CHECK(OtaTree::stored(t.stream(release, 1, 508)) == 509);   // and the largest one attribute holds
}

TEST_CASE("BLE firmware update: the connection that began a transfer owns it, and no other connection can touch it") {
    OtaTree t;
    const auto release = otaRelease(otaImage(600));
    REQUIRE(OtaTree::ok(t.begin(release)));
    REQUIRE(OtaTree::stored(t.stream(release, 0, 288)) == 288);

    t.select(1);   // a second phone: its packets are dropped and earn no answer
    CHECK(t.stream(release, 288, 288).empty());
    CHECK(t.stream(release, 0, 288).empty());
    CHECK(OtaTree::error(t.call("ota-end")) == "ota-owner");
    CHECK(OtaTree::error(t.call("ota-abort")) == "ota-owner");
    CHECK(OtaTree::error(t.begin(release)) == "ota-busy");
    const auto status = t.call("ota-status");
    CHECK(replyString(status, "phase") == "idle");
    CHECK(replyNumber(status, "received") == 0);
    CHECK(replyNumber(status, "total") == 0);

    t.select(0);   // the owner's transfer is untouched
    CHECK(t.phase() == "uploading");
    CHECK(t.slot.image.size() == 288);
    CHECK(t.slot.aborts == 0);
    CHECK(OtaTree::stored(t.stream(release, 288, 288)) == 576);
}

TEST_CASE("BLE firmware update: a connection that never began one holds none") {
    OtaTree t;
    const auto release = otaRelease(otaImage(10));
    CHECK(t.stream(release, 0, 10).empty());
    CHECK(OtaTree::error(t.call("ota-end")) == "ota-owner");
    CHECK(OtaTree::error(t.call("ota-abort")) == "ota-owner");
    CHECK(t.phase() == "idle");
}

TEST_CASE("BLE firmware update: the owner aborts its transfer and the slot is free for the next") {
    OtaTree t;
    const auto release = otaRelease(otaImage(600));
    REQUIRE(OtaTree::ok(t.begin(release)));
    REQUIRE(OtaTree::stored(t.stream(release, 0, 288)) == 288);
    CHECK(OtaTree::ok(t.call("ota-abort")));
    CHECK(t.slot.aborts == 1);
    CHECK(t.slot.commits == 0);
    CHECK_FALSE(t.slot.open);
    const auto status = t.call("ota-status");
    CHECK(replyString(status, "phase") == "aborted");
    CHECK(replyNumber(status, "received") == 288);
    CHECK(t.stream(release, 288, 288).empty());
    CHECK(t.slot.image.size() == 288);
    CHECK(OtaTree::error(t.call("ota-end")) == "ota-owner");
    // Another phone may now begin, and so may this one again.
    t.select(1);
    CHECK(OtaTree::ok(t.begin(release)));
    CHECK(t.slot.begins == 2);
}

TEST_CASE("BLE firmware update: a second begin by the owner is busy, not a restart") {
    OtaTree t;
    const auto release = otaRelease(otaImage(600));
    REQUIRE(OtaTree::ok(t.begin(release)));
    REQUIRE(OtaTree::stored(t.stream(release, 0, 288)) == 288);
    CHECK(OtaTree::error(t.begin(release)) == "ota-busy");
    CHECK(t.slot.begins == 1);
    CHECK(OtaTree::stored(t.stream(release, 288, 288)) == 576);
}

TEST_CASE("BLE firmware update: disconnecting aborts the transfer, and no connection resumes it") {
    OtaTree t;
    const auto release = otaRelease(otaImage(600));
    REQUIRE(OtaTree::ok(t.begin(release)));
    REQUIRE(OtaTree::stored(t.stream(release, 0, 288)) == 288);
    REQUIRE(t.slot.open);

    // Only the second phone is still connected; the next due-poll finds the owner gone.
    const uint64_t connected[] = {20};
    t.tree.service.reconcileOwnersForTest(connected, 1);
    t.tree.service.applyDue(1500);
    CHECK(t.slot.aborts == 1);
    CHECK_FALSE(t.slot.open);
    CHECK(t.slot.commits == 0);

    t.select(0);   // the dropped connection has no session left to ask with
    CHECK(OtaTree::error(t.call("ota-status", "", 1600)) == "session-required");
    t.select(1);
    CHECK(t.phase() == "idle");
    CHECK(OtaTree::ok(t.begin(release)));   // the slot is free again
}

TEST_CASE("BLE firmware update: a connection that is still carried keeps its transfer through the poll") {
    OtaTree t;
    const auto release = otaRelease(otaImage(600));
    REQUIRE(OtaTree::ok(t.begin(release)));
    const uint64_t connected[] = {UINT64_MAX, 20};
    t.tree.service.reconcileOwnersForTest(connected, 2);
    t.tree.service.applyDue(1500);
    CHECK(t.slot.aborts == 0);
    CHECK(t.phase() == "uploading");
}

TEST_CASE("BLE firmware update: thirty seconds without stored data abandon the transfer") {
    OtaTree t;
    const auto release = otaRelease(otaImage(600));
    REQUIRE(OtaTree::ok(t.begin(release, 1000)));
    REQUIRE(OtaTree::stored(t.stream(release, 0, 288, 5000)) == 288);

    t.tree.service.applyDue(5000 + 29999);
    CHECK(t.slot.aborts == 0);
    // A status poll is not progress, so it cannot keep the slot busy.
    t.select(0);
    CHECK(t.phase() == "uploading");
    t.tree.service.applyDue(5000 + 30000);
    CHECK(t.slot.aborts == 1);
    CHECK_FALSE(t.slot.open);
    CHECK(t.stream(release, 288, 288, 36000).empty());
    CHECK(t.phase() == "aborted");
}

TEST_CASE("BLE firmware update: each stored window restarts the idle clock") {
    OtaTree t;
    const auto release = otaRelease(otaImage(864));
    REQUIRE(OtaTree::ok(t.begin(release, 1000)));
    REQUIRE(OtaTree::stored(t.stream(release, 0, 288, 21000)) == 288);
    REQUIRE(OtaTree::stored(t.stream(release, 288, 288, 41000)) == 576);
    t.tree.service.applyDue(70999);
    CHECK(t.slot.aborts == 0);
    CHECK(OtaTree::stored(t.stream(release, 576, 288, 70999)) == 864);
}

TEST_CASE("BLE firmware update: a transfer whose bytes differ from the signed digest never commits") {
    OtaTree t;
    const auto release = otaRelease(otaImage(600));
    const auto other = otaImage(600, 2);   // the same size, but not the image that was signed
    REQUIRE(OtaTree::ok(t.begin(release)));
    REQUIRE(OtaTree::stored(t.stream(0, other.data(), other.size())) == 600);
    CHECK(OtaTree::error(t.call("ota-end")) == "ota-integrity");
    CHECK(t.slot.commits == 0);
    CHECK(t.slot.aborts == 1);
    CHECK_FALSE(t.tree.service.restartScheduledForTest());
    CHECK(t.phase() == "failed");
    CHECK(OtaTree::ok(t.begin(release)));   // and the genuine image can still be sent
}

TEST_CASE("BLE firmware update: ending before every byte arrived leaves the transfer open to finish") {
    OtaTree t;
    const auto release = otaRelease(otaImage(600));
    REQUIRE(OtaTree::ok(t.begin(release)));
    REQUIRE(OtaTree::stored(t.stream(release, 0, 288)) == 288);
    CHECK(OtaTree::error(t.call("ota-end")) == "ota-incomplete");
    CHECK(t.slot.commits == 0);
    CHECK(t.phase() == "uploading");
    CHECK_FALSE(t.tree.service.restartScheduledForTest());
    REQUIRE(OtaTree::stored(t.stream(release, 288, 312)) == 600);
    CHECK(OtaTree::ok(t.call("ota-end")));
}

TEST_CASE("BLE firmware update: bytes beyond the signed size end the stream, and the answer says it failed") {
    OtaTree t;
    const auto release = otaRelease(otaImage(300));
    REQUIRE(OtaTree::ok(t.begin(release)));
    REQUIRE(OtaTree::stored(t.stream(release, 0, 288)) == 288);
    const auto more = otaImage(24);
    const auto acks = t.stream(288, more.data(), 24);
    REQUIRE(acks.size() == 1);
    CHECK(acks[0].failed);
    CHECK(acks[0].stored == 288);
    CHECK(t.stream(release, 288, 12).empty());   // the stream is closed
    CHECK(OtaTree::ok(t.call("ota-abort")));
}

TEST_CASE("BLE firmware update: a flash failure ends the transfer, and the next begin is not wedged by it") {
    OtaTree t;
    const auto release = otaRelease(otaImage(600));
    REQUIRE(OtaTree::ok(t.begin(release)));
    t.slot.failWrite = true;
    t.slot.failedAborts = 2;   // the platform had already released the writer, so its own abort reports nothing left
    const auto acks = t.stream(release, 0, 288);
    REQUIRE(acks.size() == 1);
    CHECK(acks[0].failed);
    CHECK(t.phase() == "failed");
    t.slot.failWrite = false;
    // Cleanup is retried before the next begin and only unblocks it once the writer is confirmed released.
    CHECK(OtaTree::error(t.begin(release)) == "ota-io");
    CHECK(OtaTree::ok(t.begin(release)));
    CHECK(t.slot.begins == 2);
    CHECK(t.streamAll(release) == 600);
    CHECK(OtaTree::ok(t.call("ota-end")));
    CHECK(t.slot.image == release.image);
}

TEST_CASE("BLE firmware update: a commit the platform refuses is an I/O error, and nothing restarts") {
    OtaTree t;
    const auto release = otaRelease(otaImage(300));
    REQUIRE(OtaTree::ok(t.begin(release)));
    REQUIRE(t.streamAll(release) == 300);
    t.slot.failEnd = true;
    CHECK(OtaTree::error(t.call("ota-end")) == "ota-io");
    CHECK_FALSE(t.tree.service.restartScheduledForTest());
    CHECK(t.phase() == "failed");
}

TEST_CASE("BLE firmware update: the operations take exactly their own fields") {
    OtaTree t;
    const auto release = otaRelease(otaImage(300));
    CHECK(OtaTree::error(t.call("ota-begin", ",\"$claims\":\"" + release.claims + "\",\"$sig\":\"" + release.signature + "\",\"$offset\":0")) == "fields");
    CHECK(OtaTree::error(t.call("ota-begin", ",\"$data\":\"AAAA\"")) == "fields");
    CHECK(t.slot.begins == 0);
    REQUIRE(OtaTree::ok(t.begin(release)));
    CHECK(OtaTree::error(t.call("ota-end", ",\"$offset\":0")) == "fields");
    CHECK(OtaTree::error(t.call("ota-abort", ",\"$sig\":\"x\"")) == "fields");
    CHECK(OtaTree::error(t.call("ota-status", ",\"$target\":{\"s\":1,\"id\":1}")) == "fields");
    CHECK(OtaTree::error(t.call("ota-erase")) == "operation");
    CHECK(t.phase() == "uploading");
}

TEST_CASE("BLE firmware update: the platform slot reports the shared gate, and a writer it never held counts as aborted") {
    struct Gate { ~Gate() { mm::platform::setTestOtaBusy(false); } } gate;
    mm::PlatformFirmwareSlot slot;
    CHECK_FALSE(slot.busy());
    CHECK(slot.abort());   // nothing open and the gate free: nothing is held
    mm::platform::setTestOtaBusy(true);
    CHECK(slot.busy());
    CHECK_FALSE(slot.abort());   // the gate says something else still holds a writer
    CHECK_FALSE(slot.begin(100));   // the desktop has no slot to open
    CHECK_FALSE(slot.write(nullptr, 0));
    CHECK_FALSE(slot.end(true));
}

TEST_CASE("BLE firmware update: releasing the service while a transfer is open aborts it") {
    FakeSlot slot;
    mm::platform::setTestFirmwareSignatureCheck(&otaSignatureCheck);
    {
        BleTree tree;
        tree.service.setIdentity("nebulite-esp32", "f5a244a9+");
        tree.service.setFirmwareRelease(kOtaKey, kOtaSecurityVersion);
        tree.service.setFirmwareSlotForTest(&slot);
        const auto release = otaRelease(otaImage(300));
        tree.send(("{\"$v\":1,\"$id\":2,\"$op\":\"ota-begin\",\"$claims\":\"" + release.claims + "\",\"$sig\":\"" + release.signature + "\"}").c_str(), 1000);
        REQUIRE(slot.open);
    }
    mm::platform::setTestFirmwareSignatureCheck(nullptr);
    CHECK_FALSE(slot.open);
    CHECK(slot.aborts == 1);
}

TEST_CASE("BLE patches under Drivers reach only power, brightness and a driver's fixture") {
    BleTree tree;
    for (const char* request : {
        R"({"$v":1,"$id":21,"$op":"patch","Drivers":{"RGB1":{"type":"PwmLightDriver"}}})",
        R"({"$v":1,"$id":22,"$op":"patch","Drivers":{"RGB1":{"pin":4}}})",
        R"({"$v":1,"$id":23,"$op":"patch","Drivers":{"$patch":"replace"}})",
        R"({"$v":1,"$id":24,"$op":"patch","Drivers":{"RGB1":{"fixture":"GRB","pin":4}}})",
        R"({"$v":1,"$id":25,"$op":"patch","Drivers":{"RGB1":{"fixture":3}}})",
        R"({"$v":1,"$id":26,"$op":"patch","Drivers":{"on":"yes"}})",
        R"({"$v":1,"$id":27,"$op":"patch","Drivers":{}})"}) {
        tree.send(request, 100);
        CHECK(tree.reply().find("scope") != std::string::npos);
    }
    for (const char* request : {
        R"({"$v":1,"$id":28,"$op":"patch","Drivers":{"RGB1":{"fixture":"GRB"},"RGB2":{"fixture":"GRB"}}})",
        R"({"$v":1,"$id":29,"$op":"patch","Drivers":{"on":true,"brightness":80}})"}) {
        tree.send(request, 100);
        CHECK(tree.reply().find("scope") == std::string::npos);
    }
}
