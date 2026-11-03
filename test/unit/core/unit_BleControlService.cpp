/// @module BleControlService
#include "doctest.h"
#include "core/services/BleControlService.h"
#include "helio/BleAdvertisementRegistry.h"
#include "helio/BlePixelMapPage.h"
#include "light/layouts/Layouts.h"
#include "core/services/BatteryService.h"
#include "core/system/ControlModule.h"
#include "core/util/ModuleFactory.h"
#include "core/util/BlePeerTable.h"
#include "platform/platform.h"
#include "core/util/build_info.h"
#include "module_types.h"
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

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
    ~BleTree() { scheduler.release(); }
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
    // Only the second pack recovers: RPC2 and BAS must agree on its physical
    // identity without making the first pack appear fresh or discharged.
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
    if (!mm::isProductFirmware(mm::kFirmwareName)) return;
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
    send(10, get(first, 2), 102); const std::string aReply = peers.read(10, 0);
    send(20, get(second, 2), 103); const std::string bReply = peers.read(20, 0);
    checkEnvelope(aReply, 2, first, "get"); checkEnvelope(bReply, 2, second, "get");
    REQUIRE(peers.disconnect(10)); REQUIRE(peers.connect(10));
    CHECK(peers.publish("late reply", genA) == Publish::Stale);
    CHECK(std::string(peers.read(20, 0)) == bReply);
    // Mirror the service tick's active-owner reconciliation before the next request.
    uint64_t live[2] = {}; REQUIRE(peers.generations(live, 2) == 2);
    tree.service.reconcileOwnersForTest(live, 2);
    // The still-connected second client continues with its original namespace.
    send(20, get(second, 3), 104); checkEnvelope(tree.reply(), 3, second, "get");
    send(10, helloA, 105); CHECK(replyNumber(tree.reply(), "s") == first);
    send(10, get(first, 2), 106); CHECK(std::string(peers.read(10, 0)) == aReply);
}

TEST_CASE("BLE RPC2 publishes a due group result separately from another peer's read") {
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
    CHECK(std::string(peers.read(10, 0)) == terminal);
    CHECK(std::string(peers.read(20, 0)) == tree.reply());
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
    // MINI80 is a distinct, still-unverified firmware target; an explicit
    // diameter is metadata rather than proof of the physical board.
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
    CHECK(status.missingPeerReads == 0);
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
